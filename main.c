/* TODO
 * - magari fare una macro che presa una struttura ritorna il rect corrispondente usando pos e size come campi
 * - spawn della stanza per ogni stanza
*/

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include "raylib.h"
#include "raymath.h"
#include "/home/mathieu/Coding/C/libs/dynamic_arrays.h"

#define NOB_IMPLEMENTATION
#include "nob.h"

/// Macros
#define UNUSED(x) (void)(x)

#define UNIMPLEMENTED(x)                      \
    do {                                      \
        printf("NOT IMPLEMENTED: %s\n", (x)); \
        exit(1);                              \
    } while (0)

#define JUMP_FORCE 500
#define PLAYER_VELOCITY 300
///

/// Data Structures
typedef struct
{
    Vector2 pos;
    Vector2 size;
} Ground;
typedef struct {
    Ground *items;
    size_t count;
    size_t capacity;
} Grounds;

typedef struct
{
    Vector2 pos;
    Vector2 size;
} Wall;
typedef struct {
    Wall *items;
    size_t count;
    size_t capacity;
} Walls;

typedef struct
{
    Vector2 pos;
    Vector2 size;
    size_t takes_to;
    bool spawn_left;
} Door;
typedef struct {
    Door *items;
    size_t count;
    size_t capacity;
} Doors;

typedef struct
{
    Grounds grounds;
    Walls walls;
    Doors doors;
} Room;

typedef struct {
    Room *items;
    size_t count;
    size_t capacity;
} Rooms;

typedef enum
{
    DIR_LEFT = -1,
    DIR_FRONT = 0,
    DIR_RIGHT = 1
} MoveDirection;

typedef struct
{
    Vector2 pos;
    Vector2 size;
    Vector2 vel;
    MoveDirection direction;

    bool moving;
    bool jumping;
} Player;

typedef enum
{
    PLAYING,
    BUILDING
} GameState;

///

/// Global Variables
static GameState game_state = PLAYING;
static bool debug = false;
static bool game_pause = false;
static Vector2 *dragged_obj = NULL;
static bool adding_obj = false;

static size_t screen_width = 1920;
static size_t screen_height = 1280;
static Camera2D camera = {0};
static Player player = {0};
static const float gravity = 981.f;
/// 

/// Rooms
static size_t current_room = 0;
Rooms rooms = {0};
static inline Room *get_room(size_t n) { return &rooms.items[n]; }
#define CURRENT_ROOM (get_room(current_room))
#define ROOMS_FILEPATH "./rooms.c"
#include ROOMS_FILEPATH

void save_rooms(void)
{
    Nob_String_Builder sb = {0}; 
    nob_sb_append_cstr(&sb, "void load_rooms(void)\n");
    nob_sb_append_cstr(&sb, "{\n");

    nob_sb_append_cstr(&sb, "    Room r;\n");
    nob_sb_append_cstr(&sb, "\n");

    for (size_t i = 0; i < rooms.count; i++) {
        const Room *r = get_room(i);
        nob_sb_appendf(&sb,     "    // Room %zu\n", i);
        nob_sb_append_cstr(&sb, "    {\n");
        nob_sb_append_cstr(&sb, "        r = (Room){0};\n");

        if (!da_is_empty(&r->grounds)) {
            nob_sb_append_cstr(&sb,     "        // Grounds\n");
            for (size_t g = 0; g < r->grounds.count; g++) {
                nob_sb_appendf(&sb, "        da_push(&r.grounds, ((Ground){{%f,%f},{%f,%f}}));\n",
                        r->grounds.items[g].pos.x,
                        r->grounds.items[g].pos.y,
                        r->grounds.items[g].size.x,
                        r->grounds.items[g].size.y
                        );
            }
            nob_sb_append_cstr(&sb, "        da_fit(&r.grounds);\n");
        }

        if (!da_is_empty(&r->walls)) {
            nob_sb_append_cstr(&sb,     "        // Walls\n");
            for (size_t w = 0; w < r->walls.count; w++) {
                nob_sb_appendf(&sb, "        da_push(&r.walls, ((Wall){{%f,%f},{%f,%f}}));\n",
                        r->walls.items[w].pos.x,
                        r->walls.items[w].pos.y,
                        r->walls.items[w].size.x,
                        r->walls.items[w].size.y
                        );
            }
            nob_sb_append_cstr(&sb, "        da_fit(&r.walls);\n");
        }

        if (!da_is_empty(&r->doors)) {
            nob_sb_append_cstr(&sb,     "        // Doors\n");
            for (size_t d = 0; d < r->doors.count; d++) {
                nob_sb_appendf(&sb, "        da_push(&r.doors, ((Door){{%f,%f},{%f,%f}, %zu, %s}));\n",
                        r->doors.items[d].pos.x,
                        r->doors.items[d].pos.y,
                        r->doors.items[d].size.x,
                        r->doors.items[d].size.y,
                        r->doors.items[d].takes_to,
                        r->doors.items[d].spawn_left ? "true" : "false"
                );
            }
            nob_sb_append_cstr(&sb, "        da_fit(&r.doors);\n");
        }

        nob_sb_append_cstr(&sb, "\n        da_push(&rooms, r);\n");
        nob_sb_append_cstr(&sb, "    }\n\n");
    }

    nob_sb_append_cstr(&sb, "    da_fit(&rooms);\n\n");
    nob_sb_append_cstr(&sb, "}\n");

    if (!nob_write_entire_file(ROOMS_FILEPATH, sb.items, sb.count)) {
        // TODO: handle write failure
    }

    TraceLog(LOG_INFO, "Saved rooms to %s", ROOMS_FILEPATH);
    nob_sb_free(sb);
}

/// Collisions
static inline Rectangle rect_from_v2(Vector2 pos, Vector2 size)
{
    return (Rectangle){.x=pos.x, .y=pos.y, .width=size.x, .height=size.y};
}

/// Room Functions
static inline Rectangle ground_rect(Ground g) { return rect_from_v2(g.pos, g.size); }
static inline Rectangle wall_rect(Wall w)     { return rect_from_v2(w.pos, w.size); }
static inline Rectangle door_rect(Door d)     { return rect_from_v2(d.pos, d.size); }

void room_draw()
{
    for (size_t i = 0; i < CURRENT_ROOM->grounds.count; i++) { 
        DrawRectangleRec(ground_rect(CURRENT_ROOM->grounds.items[i]), DARKGREEN);
    }

    for (size_t i = 0; i < CURRENT_ROOM->walls.count; i++) { 
        DrawRectangleRec(wall_rect(CURRENT_ROOM->walls.items[i]), BLACK);
    }

    for (size_t i = 0; i < CURRENT_ROOM->doors.count; i++) { 
        const Door *d = &CURRENT_ROOM->doors.items[i];
        DrawRectangleRec(door_rect(*d), RED);
        char buffer[32] = {0};
        sprintf(buffer, "%zu", i);
        DrawText(buffer, d->pos.x + 10, d->pos.y + 10, 20, DARKBROWN);
        memset(buffer, 0, sizeof(buffer));
        sprintf(buffer, "> %zu", d->takes_to);
        DrawText(buffer, d->pos.x + 10, d->pos.y + 30, 20, DARKBROWN);
    }
}
///

/// Player Functions
static inline Vector2 GetMousePositionRelativeToCamera(void) { return GetScreenToWorld2D(GetMousePosition(), camera); }

void player_init(void)
{
    player.pos  = (Vector2){200.f, 400.f};
    player.size = (Vector2){25.f, 50.f};
    player.vel  = (Vector2){PLAYER_VELOCITY, 0.f};

    camera.target = Vector2Add(player.pos, Vector2Scale(player.size, .5f));
}

void player_respawn(Vector2 checkpoint)
{
    player.pos = checkpoint;
}

void player_draw(void)
{
    DrawRectangleV(player.pos, player.size, DARKBLUE); 
}

static inline Rectangle player_rect(void) { return rect_from_v2(player.pos, player.size); }

Ground *player_check_ground_collision()
{
    const Room *room = CURRENT_ROOM;
    for (size_t i = 0; i < room->grounds.count; i++) { 
        Ground *g = &room->grounds.items[i];
        if (CheckCollisionRecs(ground_rect(*g), player_rect())) return g;
    }
    return NULL;
}

Door *player_check_door_collision()
{
    const Room *room = CURRENT_ROOM;
    for (size_t i = 0; i < room->doors.count; i++) { 
        Door *d = &room->doors.items[i];
        if (CheckCollisionRecs(door_rect(*d), player_rect())) return d;
    }
    return NULL;
}

void player_handle_controls()
{
    player.moving = false;
    player.direction = DIR_FRONT;

    if (IsKeyDown(KEY_Z)) {
        player.moving = true;
        player.direction = DIR_LEFT;
    } else if (IsKeyDown(KEY_C)) {
        player.moving = true;
        player.direction = DIR_RIGHT;
    }

    if (IsKeyPressed(KEY_SPACE) && !player.jumping && player_check_ground_collision() != NULL) {
        player.jumping = true;
        player.vel.y = -JUMP_FORCE; // TODO: non sono convinto del nome JUMP_FORCE
    }
}

Wall *player_check_wall_collision()
{
    const Room *room = CURRENT_ROOM;
    for (size_t i = 0; i < room->walls.count; i++) { 
        Wall *w = &room->walls.items[i];
        if (CheckCollisionRecs(wall_rect(*w), player_rect())) return w;
    }
    return NULL;
}

void player_move_and_collide_y(float dt)
{
    player.pos.y += player.vel.y * dt;

    if (player.pos.y > screen_height) {
        player_respawn((Vector2){100, 100});
    }

    Ground *ground = player_check_ground_collision();

    if (ground) {
        player.jumping = false;
        player.vel.y = 0.f;
        player.pos.y = ground->pos.y - player.size.y + 0.1;
    } else player.vel.y += gravity * dt;
}

void player_move_and_collide_x(float dt)
{
    if (player.direction == DIR_FRONT) return;

    player.pos.x += player.vel.x * player.direction * dt;

    Wall *wall = player_check_wall_collision();

    if (wall) {
        if (player.direction == DIR_LEFT) player.pos.x = wall->pos.x + wall->size.x;
        else player.pos.x = wall->pos.x - player.size.x;
    }
}

void player_check_move_through_door(void)
{
    Door *d_from = player_check_door_collision();
    if (!d_from) return;
    size_t new_room = d_from->takes_to;

    // TODO: maybe store this information in the door itself?
    Door *d_to = NULL;
    for (size_t i = 0; i < get_room(new_room)->doors.count; i++) {
        if (get_room(new_room)->doors.items[i].takes_to == current_room) {
            d_to = &get_room(new_room)->doors.items[i];
            break;
        }
    }

    if (d_to == NULL) {
        TraceLog(LOG_WARNING, "No door connection from %zu to %zu", current_room, new_room);
        return;
    }

    current_room = new_room;


    if (d_to->spawn_left)
        player.pos = (Vector2){d_to->pos.x - player.size.x - 10.f,  d_to->pos.y + d_to->size.y - player.size.y};
    else
        player.pos = (Vector2){d_to->pos.x + d_to->size.x  + 10.f,  d_to->pos.y + d_to->size.y - player.size.y};

    player.direction = d_to->spawn_left ? DIR_LEFT : DIR_RIGHT;
    player.moving = false;
    player.jumping = false;
    player.vel.y = 0.f;
    // TODO: timer for not receiving inputs
}

void player_update(float dt)
{
    if (game_state == PLAYING) player_handle_controls();
    player_move_and_collide_y(dt);
    player_move_and_collide_x(dt);
    player_check_move_through_door();
}

void camera_update(void)
{
    if (game_state == BUILDING) {
        float wheel = GetMouseWheelMove();
        if (fabsf(wheel) > 1e-6) camera.zoom = expf(logf(camera.zoom) + wheel*0.1f);
        if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) return;
        if (IsKeyDown(KEY_Z)) camera.target.x -= 10;
        if (IsKeyDown(KEY_C)) camera.target.x += 10;
        if (IsKeyDown(KEY_S)) camera.target.y -= 10;
        if (IsKeyDown(KEY_X)) camera.target.y += 10;
    } else {
        camera.target = Vector2Add(player.pos, Vector2Scale(player.size, .5f));
    }
}
///

/// Game Initialization
void game_init(void)
{
    screen_width = GetScreenWidth();
    screen_height = GetScreenHeight();

    load_rooms();

    camera.offset = (Vector2){ screen_width/2.f, screen_height/2.f };
    camera.rotation = 0.f;
    camera.zoom = 1.f;
}
///

/// Modes
void building_mode(void)
{
    if (IsKeyPressed(KEY_A)) {
        adding_obj = !adding_obj;
        TraceLog(LOG_INFO, "%s add obj mode", adding_obj ? "Entering" : "Exiting");
    } else if (adding_obj) {
        switch (GetKeyPressed())
        {
        case KEY_G: {
            Ground g = {0};
            g.pos = GetMousePositionRelativeToCamera();
            g.size = (Vector2){100, 50};
            da_push(&CURRENT_ROOM->grounds, g); 
        } break;
        case KEY_W: {
            Wall w = {0};
            w.pos = GetMousePositionRelativeToCamera();
            w.size = (Vector2){50, 100};
            da_push(&CURRENT_ROOM->walls, w); 
        } break;
        case KEY_D: {
            Door d = {0};
            d.pos = GetMousePositionRelativeToCamera();
            d.size = (Vector2){50, 100};
            da_push(&CURRENT_ROOM->doors, d); 
            // TODO: add mechanism to change:
            // - spawning direction
            // - room connection
        } break;
        case KEY_R: {
            current_room = rooms.count;
            da_push(&rooms, ((Room){0}));
        } break;
        }
    } else if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
        if (IsKeyPressed(KEY_S)) save_rooms();
        else if (IsKeyPressed(KEY_P) && current_room+1 < rooms.count) current_room++;
        else if (IsKeyPressed(KEY_N) && current_room > 0) current_room--;
    } else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 mouse = GetMousePositionRelativeToCamera();
        if (CheckCollisionPointRec(mouse, player_rect())) {
            dragged_obj = &player.pos;
        } else {
            for (size_t i = 0; i < CURRENT_ROOM->grounds.count; i++) {
                if (CheckCollisionPointRec(mouse, ground_rect(CURRENT_ROOM->grounds.items[i]))) {
                    dragged_obj = &CURRENT_ROOM->grounds.items[i].pos;
                    break;
                }
            }
            if (dragged_obj == NULL) {
                for (size_t i = 0; i < CURRENT_ROOM->walls.count; i++) {
                    if (CheckCollisionPointRec(mouse, wall_rect(CURRENT_ROOM->walls.items[i]))) {
                        dragged_obj = &CURRENT_ROOM->walls.items[i].pos;
                        break;
                    }
                }
            }
            if (dragged_obj == NULL) {
                for (size_t i = 0; i < CURRENT_ROOM->doors.count; i++) {
                    if (CheckCollisionPointRec(mouse, door_rect(CURRENT_ROOM->doors.items[i]))) {
                        dragged_obj = &CURRENT_ROOM->doors.items[i].pos;
                        break;
                    }
                }
            }
        }
    } else if (dragged_obj && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        *dragged_obj = GetMousePositionRelativeToCamera();
    } else if (dragged_obj && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        *dragged_obj = GetMousePositionRelativeToCamera();
        dragged_obj = NULL;
    }
}
///

/// Main
int main(void)
{
    InitWindow(800, 600, "MyGame");
    SetWindowState(/*FLAG_FULLSCREEN_MODE | */FLAG_WINDOW_RESIZABLE);
    SetTargetFPS(60);

    game_init();
    player_init();

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        if (!adding_obj && IsKeyPressed(KEY_D)) debug = !debug;
        if (!(IsKeyDown(KEY_LEFT_CONTROL)) && IsKeyPressed(KEY_P)) game_pause = !game_pause;
        if (IsKeyPressed(KEY_B)) {
            if (game_state == BUILDING) game_state = PLAYING;
            else if (game_state == PLAYING) game_state = BUILDING;
        }

        // TODO: factor out this block
        if (game_state == BUILDING) {
            building_mode();
        }

        if (!game_pause) player_update(dt);
        camera_update();

        BeginDrawing();
        ClearBackground(GRAY);

        BeginMode2D(camera);

        room_draw();
        player_draw();

        EndMode2D();

        if (game_pause) DrawText("PAUSE", screen_width-100, 10, 20, BLACK);
        if (debug) {
            DrawText("DEBUG", screen_width-100, 30, 20, BLACK);

            size_t line = 1;
            char buffer[64];
            sprintf(buffer, "FPS: %d", GetFPS());
            DrawText(buffer, 25, 25*line++, 20, BLACK);
            memset(buffer, 0, sizeof(buffer));

            Vector2 mouse = GetMousePosition();
            sprintf(buffer, "mouse abs: (%.2f, %.2f)", mouse.x, mouse.y);
            DrawText(buffer, 25, 25*line++, 20, BLACK);
            memset(buffer, 0, sizeof(buffer));

            mouse = GetMousePositionRelativeToCamera();
            sprintf(buffer, "mouse rel: (%.2f, %.2f)", mouse.x, mouse.y);
            DrawText(buffer, 25, 25*line++, 20, BLACK);
        }
        if (game_state == BUILDING) {
            DrawText("BUILDING", screen_width-100, 50, 20, BLACK);
            if (adding_obj) {
                size_t line = 1;
                DrawText("room (r)", 10, screen_height - 25*line++, 20, BLACK);
                DrawText("door (d)", 10, screen_height - 25*line++, 20, BLACK);
                DrawText("wall (w)", 10, screen_height - 25*line++, 20, BLACK);
                DrawText("ground (g)", 10, screen_height - 25*line++, 20, BLACK);
                DrawText("ADD",        10, screen_height - 25*line++, 20, BLACK);
            }
        }

        char buffer[64];
        sprintf(buffer, "Room %zu", current_room);
        DrawText(buffer, screen_width-100, screen_height-25, 20, BLACK);

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
