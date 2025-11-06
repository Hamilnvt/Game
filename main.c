/* TODO
 * - magari fare una macro che presa una struttura ritorna il rect corrispondente usando pos e size come campi
 * - spawn della stanza per ogni stanza
 * - componente Transform
 * - don't quit there are changes (made in BUILDING)
*/

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
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
} Block;
typedef struct {
    Block *items;
    size_t count;
    size_t capacity;
} Blocks;

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
    Blocks blocks;
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

typedef enum
{
    OBJ_NONE = 0,
    OBJ_PLAYER,
    OBJ_BLOCK,
    OBJ_DOOR,
    OBJTYPES_COUNT
} ObjectType;

static_assert(OBJTYPES_COUNT == 4, "ObjectTypeToString does not cover all object types");
const char *ObjectTypeToString(ObjectType type)
{
    switch (type)
    {
    case OBJ_NONE:   return "None";
    case OBJ_PLAYER: return "Player";
    case OBJ_BLOCK:  return "Block";
    case OBJ_DOOR:   return "Door";
    default: abort();
    }
}

typedef struct
{
    bool active;
    Vector2 *pos;
    Vector2 *size;
    size_t index;
    ObjectType type;

    bool dragging;
    bool resizing;
} SelectedObject;

///

/// Global Variables
static GameState game_state = PLAYING;
static bool modified = false;
static bool debug = false;
static bool game_pause = false;
static SelectedObject selected_obj = {0};
static bool adding_obj = false;

static size_t screen_width = 1920;
static size_t screen_height = 1280;
static Camera2D camera = {0};
static Player player = {0};
static const float gravity = 1200.f;
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

        if (!da_is_empty(&r->blocks)) {
            nob_sb_append_cstr(&sb,     "        // Blocks\n");
            for (size_t w = 0; w < r->blocks.count; w++) {
                nob_sb_appendf(&sb, "        da_push(&r.blocks, ((Block){{%f,%f},{%f,%f}}));\n",
                        r->blocks.items[w].pos.x,
                        r->blocks.items[w].pos.y,
                        r->blocks.items[w].size.x,
                        r->blocks.items[w].size.y
                        );
            }
            nob_sb_append_cstr(&sb, "        da_fit(&r.blocks);\n");
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

    nob_write_entire_file(ROOMS_FILEPATH, sb.items, sb.count);

    TraceLog(LOG_INFO, "Saved rooms to %s", ROOMS_FILEPATH);
    nob_sb_free(sb);
}

/// Collisions
static inline Rectangle rect_from_v2(Vector2 pos, Vector2 size)
{
    return (Rectangle){.x=pos.x, .y=pos.y, .width=size.x, .height=size.y};
}
static inline Rectangle selected_obj_rect(void) { return rect_from_v2(*selected_obj.pos, *selected_obj.size); }
static inline Rectangle block_rect(Block b)     { return rect_from_v2(b.pos, b.size); }
static inline Rectangle door_rect(Door d)       { return rect_from_v2(d.pos, d.size); }

/// Room Functions

void draw_arrow(int x, int y, int size, Color color, int dir)
{
    DrawRectangle(x, y, size, size, color);     
    switch (dir)
    {
    case 0: DrawTriangle((Vector2){x-size/2, y}, (Vector2){x+3*size/2, y}, (Vector2){x+size/2, y-size}, color); break;
    case 1: DrawTriangle((Vector2){x+size, y-size/2}, (Vector2){x+size, y+3*size/2}, (Vector2){x+2*size, y+size/2}, color); break;
    case 2: DrawTriangle((Vector2){x-size/2, y+size}, (Vector2){x+size/2, y+2*size}, (Vector2){x+3*size/2, y+size}, color); break;
    case 3: DrawTriangle((Vector2){x, y+3*size/2}, (Vector2){x, y-size/2}, (Vector2){x-size, y+size/2}, color); break;
    }
}

void room_draw()
{
    // Blocks
    for (size_t i = 0; i < CURRENT_ROOM->blocks.count; i++) { 
        DrawRectangleRec(block_rect(CURRENT_ROOM->blocks.items[i]), BLACK);
    }

    // Doors
    for (size_t i = 0; i < CURRENT_ROOM->doors.count; i++) { 
        const Door *d = &CURRENT_ROOM->doors.items[i];
        DrawRectangleRec(door_rect(*d), RED);
        draw_arrow(d->pos.x + (d->spawn_left ? -20 : d->size.x + 10), d->pos.y+d->size.y/2-5, 10, RED, d->spawn_left ? 3 : 1);
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

Door *player_check_door_collision()
{
    const Room *room = CURRENT_ROOM;
    for (size_t i = 0; i < room->doors.count; i++) { 
        Door *d = &room->doors.items[i];
        if (CheckCollisionRecs(door_rect(*d), player_rect())) return d;
    }
    return NULL;
}

#define COLLISION_NONE  0
#define COLLISION_UP    0x1
#define COLLISION_RIGHT 0x2
#define COLLISION_DOWN  0x4
#define COLLISION_LEFT  0x8
#define COLLISION_ALL   0xF
typedef uint8_t CollisionDirection;

Block *player_check_block_collision(CollisionDirection *dir)
{
    const Room *room = CURRENT_ROOM;
    for (size_t i = 0; i < room->blocks.count; i++) { 
        Block *b = &room->blocks.items[i];
        if (CheckCollisionRecs(block_rect(*b), player_rect())) {
            if (dir) {
                *dir = COLLISION_NONE;
                if (player.pos.x + player.size.x > b->pos.x && player.pos.x < b->pos.x + b->size.x) {
                    if (player.pos.y + player.size.y > b->pos.y && player.pos.y + player.size.y < b->pos.y + b->size.y/2) {
                        *dir |= COLLISION_UP;
                    }
                    if (player.pos.y < b->pos.y + b->size.y && player.pos.y > b->pos.y + b->size.y/2) {
                        *dir |= COLLISION_DOWN;
                    }
                }
                if (player.pos.y + player.size.y > b->pos.y && player.pos.y < b->pos.y + b->size.y) {
                    if (player.pos.x + player.size.x > b->pos.x && player.pos.x + player.size.x < b->pos.x + b->size.x/2) {
                        *dir |= COLLISION_LEFT;
                    }
                    if (player.pos.x < b->pos.x + b->size.x && player.pos.x > b->pos.x + b->size.x/2) {
                        *dir |= COLLISION_RIGHT;
                    }
                }
            }
            return b;
        }
    }
    if (dir) *dir = COLLISION_NONE;
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

    // TODO: make it jump more if holding space bar
    CollisionDirection dir;
    if (IsKeyPressed(KEY_SPACE) && !player.jumping && player_check_block_collision(&dir) != NULL && (dir & COLLISION_UP)) {
        player.jumping = true;
        player.vel.y = -JUMP_FORCE;
    }
}

void player_move_and_collide_y(float dt)
{
    player.pos.y += player.vel.y * dt;

    if (player.pos.y > screen_height) {
        player_respawn((Vector2){100, 100});
        return;
    }

    CollisionDirection dir;
    Block *block = player_check_block_collision(&dir);

    if (block && (dir & COLLISION_UP)) {
        if (player.jumping) {
            player.jumping = false;
            player.vel.y = 0.f;
        }
        player.pos.y = block->pos.y - player.size.y + .1f; // TODO: non voglio dover sommare 0.1
    } else if (block && (dir & COLLISION_DOWN)) { 
        player.pos.y = block->pos.y + block->size.y;
        player.vel.y = 0.f;
    } else player.vel.y += gravity * dt;
}

void player_move_and_collide_x(float dt)
{
    if (player.direction == DIR_FRONT) return;

    player.pos.x += player.vel.x * player.direction * dt;

    CollisionDirection dir;
    Block *block = player_check_block_collision(&dir);

    if (block && !((dir & COLLISION_UP) || (dir &COLLISION_DOWN))) {
        if (dir & COLLISION_LEFT) {
            player.pos.x = block->pos.x - player.size.x;
        } else if (dir & COLLISION_RIGHT) {
            player.pos.x = block->pos.x + block->size.x;
        }
    }
}

void player_check_move_through_door(void)
{
    Door *d_from = player_check_door_collision();
    if (!d_from) return;
    size_t new_room = d_from->takes_to;

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

void check_unsaved_changes(void)
{
    if (modified) {
        printf("\nWARNING: You have unsaved changes, wanna save? [Y/N]\n");
        int c = getchar();
        while (c != 'Y' && c != 'N') {
            c = getchar();
        }
        if (c == 'Y') save_rooms();
    }
}

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
    } else if (adding_obj) {
        switch (GetKeyPressed())
        {
        case KEY_G: {
            Block b = {0};
            b.pos = GetMousePositionRelativeToCamera();
            b.size = (Vector2){50, 100};
            da_push(&CURRENT_ROOM->blocks, b); 
        } break;
        case KEY_D: {
            Door d = {0};
            d.pos = GetMousePositionRelativeToCamera();
            d.size = (Vector2){50, 100};
            da_push(&CURRENT_ROOM->doors, d); 
            // TODO: add mechanism to change:
            // - spawning direction
            // - room connection
            // >> sarebbe carino che quando si seleziona un oggetto si possono modificare i suoi valori
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
        else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            selected_obj.active = false;
            Vector2 mouse = GetMousePositionRelativeToCamera();
            if (CheckCollisionPointRec(mouse, player_rect())) {
                selected_obj.active = true;
                selected_obj.type = OBJ_PLAYER;
                selected_obj.pos = &player.pos;
                selected_obj.size = &player.size;
            } else {

                for (size_t i = 0; i < CURRENT_ROOM->blocks.count; i++) {
                    Block *b = &CURRENT_ROOM->blocks.items[i];
                    if (CheckCollisionPointRec(mouse, block_rect(*b))) {
                        selected_obj.active = true;
                        selected_obj.type = OBJ_BLOCK;
                        selected_obj.pos = &b->pos;
                        selected_obj.size = &b->size;
                        selected_obj.index = i;
                        break;
                    }
                }

                if (!selected_obj.active) {
                    for (size_t i = 0; i < CURRENT_ROOM->doors.count; i++) {
                        Door *d = &CURRENT_ROOM->doors.items[i];
                        if (CheckCollisionPointRec(mouse, door_rect(*d))) {
                            selected_obj.active = true;
                            selected_obj.type = OBJ_DOOR;
                            selected_obj.pos = &d->pos;
                            selected_obj.size = &d->size;
                            selected_obj.index = i;
                            break;
                        }
                    }
                }

            }
        }
    } else if (selected_obj.active) {
        // TODO: ovviamente non funziona
        Vector2 mouse = GetMousePositionRelativeToCamera();
        if (selected_obj.dragging) {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
                *selected_obj.pos = Vector2Add(*selected_obj.pos, GetMouseDelta());
            else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) selected_obj.dragging = false;
        } else if (selected_obj.resizing) {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && IsKeyDown(KEY_LEFT_SHIFT))
                *selected_obj.size = Vector2Add(*selected_obj.size, GetMouseDelta());
            else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) selected_obj.dragging = false;
        }
        if (IsKeyDown(KEY_LEFT_SHIFT)) {
            if (IsKeyPressed(KEY_UP))         selected_obj.size->y -= 1;
            else if (IsKeyPressed(KEY_RIGHT)) selected_obj.size->x += 1;
            else if (IsKeyPressed(KEY_DOWN))  selected_obj.size->y += 1;
            else if (IsKeyPressed(KEY_LEFT))  selected_obj.size->x -= 1;
        } else {
            if (IsKeyPressed(KEY_UP))    selected_obj.pos->y -= 1;
            else if (IsKeyPressed(KEY_RIGHT)) selected_obj.pos->x += 1;
            else if (IsKeyPressed(KEY_DOWN))  selected_obj.pos->y += 1;
            else if (IsKeyPressed(KEY_LEFT))  selected_obj.pos->x -= 1;
        }
        if (CheckCollisionPointRec(mouse, selected_obj_rect())) {
            // TODO: prevent the object from vanishing from reality by setting a minimum size and a position boundary
            if (IsKeyDown(KEY_LEFT_SHIFT)) {
                SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);
                selected_obj.resizing = true;
                //if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
                //    //*selected_obj.size = Vector2Add(*selected_obj.size, GetMouseDelta());
                //    *selected_obj.size = GetMousePositionRelativeToCamera();
            } else {
                SetMouseCursor(MOUSE_CURSOR_RESIZE_ALL);
                selected_obj.dragging = true;
                //if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
                //    //*selected_obj.pos = Vector2Add(*selected_obj.pos, GetMouseDelta());
                //    *selected_obj.pos = GetMousePositionRelativeToCamera();
            }
        } else {
            SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        }
        if (IsKeyPressed(KEY_D)) { // TODO: maybe a way to undo it?
            switch (selected_obj.type) {
            case OBJ_BLOCK: {
                Block b; UNUSED(b);
                da_remove(&CURRENT_ROOM->blocks, selected_obj.index, b);
                selected_obj.active = false;
            } break;
            case OBJ_DOOR: {
                Door d; UNUSED(d);
                da_remove(&CURRENT_ROOM->doors, selected_obj.index, d);
                selected_obj.active = false;
            } break;
            default: TraceLog(LOG_INFO, "Cannot delete objects of type `%s`", ObjectTypeToString(selected_obj.type));
            }
        }
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

        if (IsKeyPressed(KEY_ESCAPE)) {
            TraceLog(LOG_INFO, "E' stato bello");
            if (modified) continue;
        }
            
        if (!adding_obj && IsKeyPressed(KEY_D)) debug = !debug;
        if (!(IsKeyDown(KEY_LEFT_CONTROL)) && IsKeyPressed(KEY_P)) game_pause = !game_pause;
        if (IsKeyPressed(KEY_B)) {
            if (game_state == BUILDING) {
                camera.zoom = 1.f;
                game_state = PLAYING;
            } else if (game_state == PLAYING) game_state = BUILDING;
        }

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

        if (game_state == BUILDING) {
            if (selected_obj.active) {
                DrawRectangleLinesEx((Rectangle){selected_obj.pos->x, selected_obj.pos->y, selected_obj.size->x, selected_obj.size->y}, 3, BLACK);
                DrawRectangleLinesEx((Rectangle){selected_obj.pos->x+1, selected_obj.pos->y+1, selected_obj.size->x-2, selected_obj.size->y-2}, 1, RED);
            }
        }

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
                DrawText("block (g)", 10, screen_height - 25*line++, 20, BLACK);
                DrawText("ADD",        10, screen_height - 25*line++, 20, BLACK);
            }
        }

        char buffer[64];
        sprintf(buffer, "Room %zu", current_room);
        DrawText(buffer, screen_width-100, screen_height-25, 20, BLACK);

        EndDrawing();
    }

    CloseWindow();
    check_unsaved_changes();
    return 0;
}
