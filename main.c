#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "raylib.h"
#include "raymath.h"
#include "thirdparties/cJSON/cJSON.h"
#include "dynamic_arrays.h"

#define MY_STRINGS_IMPLEMENTATION
#include "my_strings.h"

#define NOB_IMPLEMENTATION
#include "nob.h"

/// Macros
#define UNUSED(x) (void)(x)

#define NOT_IMPLEMENTED(msg)                    \
    do {                                        \
        printf("NOT IMPLEMENTED: %s\n", (msg)); \
        exit(1);                                \
    } while (0)

#define JUMP_FORCE 600
#define GROUND_YOURSELF_FORCE 20
#define PLAYER_VELOCITY 300
///

/// Data Structures

typedef enum
{
    KIND_NIL,
    KIND_PLAYER,
    KIND_ROOM,
    KIND_BLOCK,
    KIND_DOOR,
    __kinds_count
} Kind;

char *thing_kind_to_string(Kind kind)
{
    switch (kind) {
    case KIND_NIL:    return "Nil";
    case KIND_PLAYER: return "Player";
    case KIND_BLOCK:  return "Block";
    case KIND_DOOR:   return "Door";
    case KIND_ROOM:   return "Room";

    case __kinds_count:
    default:
        TraceLog(LOG_FATAL, "Unreachable thing kind %d", kind);
        exit(1);
    }
}

typedef size_t ThingIdx;
#define NIL 0
#define THING_NIL (&game.things.items[NIL])

typedef uint64_t RoomID;
#define NO_ID 0

typedef enum
{
    DIR_LEFT = -1,
    DIR_FRONT = 0,
    DIR_RIGHT = 1
} MoveDirection;

typedef enum
{
    TRAIT_NONE           = 0,
    TRAIT_NON_SELECTABLE = (1 << 0),
    TRAIT_DRAGGABLE      = (1 << 1),
    TRAIT_RESIZEABLE     = (1 << 2),
    TRAIT_NON_DELETABLE  = (1 << 3)
} Traits;

typedef struct
{
    uint64_t id;

    Kind kind;
    Traits traits;

    Vector2 pos;
    Vector2 vel;
    Vector2 size;

    // Door
    RoomID takes_to;
    bool spawn_left;

    // Player
    MoveDirection direction;
    bool is_moving;
    bool is_grounded;
    float coyote_timer;
    float disabled_movements_timer;

    // Room
    ThingIdx first_block;
    ThingIdx next_block;

    ThingIdx first_door;
    ThingIdx next_door;

} Thing;

typedef struct
{
    enum {
        PLAYING,
        BUILDING
    } state;

    bool is_dirty;
    bool debug;
    bool is_paused;

    struct {
        ThingIdx *items;
        size_t count;
        size_t capacity;

        size_t active_index;
        bool dragging;
        bool resizing;
    } selected_things;

    struct {
        // TODO: keep used list
        Thing *items;
        size_t count;
        size_t capacity;
    } things;

    size_t screen_width;
    size_t screen_height;
    Camera2D camera;
    ThingIdx player;
    ThingIdx current_room;
    float gravity;

    struct {
        bool is_shown;
        size_t cursor;
        size_t line_index;
        String *items;
        size_t count;
        size_t capacity;
    } terminal;
} Game;

static Game game;

void reset_things(void)
{
    da_clear(&game.things);
    da_push(&game.things, (Thing){0}); // Alloc THING_NIL
}

ThingIdx alloc_thing(void)
{
    ThingIdx idx;
    for (idx = 1; idx < game.things.count; idx++) {
        if (game.things.items[idx].kind == KIND_NIL) {
            return idx;
        }
    }
    da_push(&game.things, (Thing){0});
    return idx;
}

void remove_thing(ThingIdx i)
{
    if (i < game.things.count) {
        memset(&game.things.items[i], 0, sizeof(game.things.items[i]));
    }
}

Thing *get_thing(ThingIdx i)
{
    if (i < game.things.count) {
        return &game.things.items[i];
    } else {
        return THING_NIL;
    }
}

Thing *get_active_thing(void)
{
    if (game.selected_things.count == 0) {
        return THING_NIL;
    } else {
        return get_thing(game.selected_things.items[game.selected_things.active_index]);
    }
}

static inline Thing *get_player(void) { return get_thing(game.player); }
static inline Thing *get_room(ThingIdx idx) { return get_thing(idx); }
static inline ThingIdx get_room_by_id(RoomID id)
{
    for (ThingIdx idx = 1; idx < game.things.count; idx++) {
        Thing *room = get_thing(idx);
        if (room->kind != KIND_ROOM) continue;
        if (room->id == id) return idx;
    }
    return NIL;
}
#define CURRENT_ROOM (get_room(game.current_room))

ThingIdx new_player(Vector2 pos, Vector2 size, Vector2 vel)
{
    ThingIdx i = alloc_thing();
    if (i == NIL) {
        TraceLog(LOG_ERROR, "Could not allocate %s", thing_kind_to_string(KIND_PLAYER));
        abort();
    }

    game.things.items[i] = (Thing){
        .kind       = KIND_PLAYER,
        .traits     = TRAIT_DRAGGABLE | TRAIT_NON_DELETABLE,
        .pos        = pos,
        .vel        = vel,
        .size       = size
    };
    return i;
}

ThingIdx new_block(Vector2 pos, Vector2 size)
{
    ThingIdx i = alloc_thing();
    if (i == NIL) {
        TraceLog(LOG_ERROR, "Could not allocate %s", thing_kind_to_string(KIND_BLOCK));
        abort();
    }

    game.things.items[i] = (Thing){
        .kind       = KIND_BLOCK,
        .traits     = TRAIT_DRAGGABLE | TRAIT_RESIZEABLE,
        .pos        = pos,
        .size       = size,
    };
    return i;
}

ThingIdx new_door(Vector2 pos, Vector2 size, RoomID takes_to, bool spawn_left)
{
    ThingIdx i = alloc_thing();
    if (i == NIL) {
        TraceLog(LOG_ERROR, "Could not allocate %s", thing_kind_to_string(KIND_DOOR));
        abort();
    }


    game.things.items[i] = (Thing){
        .kind       = KIND_DOOR,
        .traits     = TRAIT_DRAGGABLE | TRAIT_RESIZEABLE,
        .pos        = pos,
        .size       = size,
        .takes_to   = takes_to,
        .spawn_left = spawn_left
    };
    return i;
}

static RoomID room_id_counter = 1;
#define rooms_count (room_id_counter - 1)

ThingIdx new_room(void)
{
    ThingIdx i = alloc_thing();
    if (i == NIL) {
        TraceLog(LOG_ERROR, "Could not allocate %s", thing_kind_to_string(KIND_ROOM));
        abort();
    }

    game.things.items[i] = (Thing){
        .kind   = KIND_ROOM,
        .traits = TRAIT_NON_SELECTABLE,
        .id     = room_id_counter++
    };

    return i;
}

#define _intrusive_add(to_idx, item_idx, name) \
    do {                                       \
        Thing *to = get_thing(to_idx);         \
        if (to->first_##name == NIL) {         \
            to->first_##name = (item_idx);     \
        } else {                               \
            ThingIdx t_idx = to->first_##name; \
            Thing *t = get_thing(t_idx);       \
            while (t->next_##name != NIL) {    \
                t_idx = t->next_##name;        \
                t = get_thing(t_idx);          \
            }                                  \
            t->next_##name = (item_idx);       \
        }                                      \
    } while (0)

void add_block_to_room(ThingIdx room_idx, ThingIdx block_idx)
{
    assert(get_thing(room_idx)->kind == KIND_ROOM);
    assert(get_thing(block_idx)->kind == KIND_BLOCK);

    _intrusive_add(room_idx, block_idx, block);
}

void add_door_to_room(ThingIdx room_idx, ThingIdx door_idx)
{
    assert(get_thing(room_idx)->kind == KIND_ROOM);
    assert(get_thing(door_idx)->kind == KIND_DOOR);

    _intrusive_add(room_idx, door_idx, door);
}

#define COYOTE_TIMER 0.10f
#define DISABLED_MOVEMENTS_TIMER 0.5f

///

/// Rooms

#define ROOMS_JSON_FILEPATH "./rooms.json"

bool save_rooms_to_json(void)
{
    TraceLog(LOG_INFO, "Saving rooms...");

    cJSON *root = cJSON_CreateArray();
    if (!root) return false;

    for (ThingIdx idx = 1; idx < game.things.count; idx++) {
        Thing *room = get_thing(idx);
        if (room->kind != KIND_ROOM) continue;

        cJSON *jroom = cJSON_CreateObject();
        cJSON_AddItemToArray(root, jroom);

        cJSON *blocks = cJSON_CreateArray();
        cJSON_AddItemToObject(jroom, "blocks", blocks);
        ThingIdx block_idx = room->first_block;
        Thing *block;
        while (block_idx != NIL) {
            block = get_thing(block_idx);

            cJSON *jb = cJSON_CreateObject();

            cJSON *jbpos = cJSON_CreateObject();
            cJSON_AddNumberToObject(jbpos, "x", block->pos.x);
            cJSON_AddNumberToObject(jbpos, "y", block->pos.y);
            cJSON_AddItemToObject(jb, "pos", jbpos);

            cJSON *jbsize = cJSON_CreateObject();
            cJSON_AddNumberToObject(jbsize, "x", block->size.x);
            cJSON_AddNumberToObject(jbsize, "y", block->size.y);
            cJSON_AddItemToObject(jb, "size", jbsize);

            cJSON_AddItemToArray(blocks, jb);

            block_idx = block->next_block;
        }

        cJSON *doors = cJSON_CreateArray();
        cJSON_AddItemToObject(jroom, "doors", doors);
        ThingIdx door_idx = room->first_door;
        Thing *door;
        while (door_idx != NIL) {
            door = get_thing(door_idx);

            cJSON *jd = cJSON_CreateObject();

            cJSON *jdpos = cJSON_CreateObject();
            cJSON_AddNumberToObject(jdpos, "x", door->pos.x);
            cJSON_AddNumberToObject(jdpos, "y", door->pos.y);
            cJSON_AddItemToObject(jd, "pos", jdpos);

            cJSON *jdsize = cJSON_CreateObject();
            cJSON_AddNumberToObject(jdsize, "x", door->size.x);
            cJSON_AddNumberToObject(jdsize, "y", door->size.y);
            cJSON_AddItemToObject(jd, "size", jdsize);

            cJSON_AddNumberToObject(jd, "takes_to", door->takes_to);
            cJSON_AddBoolToObject(jd, "spawn_left", door->spawn_left);

            cJSON_AddItemToArray(doors, jd);

            door_idx = door->next_door;
        }
    }

    char *printed = cJSON_Print(root); // pretty print, use cJSON_PrintUnformatted for compact
    cJSON_Delete(root);
    if (!printed) return false;

    bool ok = nob_write_entire_file(ROOMS_JSON_FILEPATH, printed, strlen(printed));
    free(printed);

    TraceLog(LOG_INFO, "OK.");

    return ok;
}

Vector2 cJSON_GetVector2(cJSON *json, const char *name)
{
    Vector2 v = {0};
    cJSON *jv = cJSON_GetObjectItemCaseSensitive(json, name);
    v.x = (float)cJSON_GetObjectItemCaseSensitive(jv, "x")->valuedouble;
    v.y = (float)cJSON_GetObjectItemCaseSensitive(jv, "y")->valuedouble;
    return v;
}

static inline ThingIdx new_block_from_json(cJSON *jb)
{
    return new_block(
        cJSON_GetVector2(jb, "pos"),
        cJSON_GetVector2(jb, "size")
    );
}

static inline ThingIdx new_door_from_json(cJSON *jd)
{
    return new_door(
        cJSON_GetVector2(jd, "pos"),
        cJSON_GetVector2(jd, "size"),
        (RoomID)cJSON_GetObjectItemCaseSensitive(jd, "takes_to")->valuedouble,
        (bool)cJSON_GetObjectItemCaseSensitive(jd, "spawn_left")->valuedouble
    );
}

#define fill_list_from_json_array(jarray, list_idx, name)          \
    do {                                                           \
        if ((jarray) && cJSON_IsArray(jarray)) {                   \
            for (int j = 0; j < cJSON_GetArraySize(jarray); j++) { \
                cJSON *jitem = cJSON_GetArrayItem((jarray), j);    \
                ThingIdx item_idx = new_##name##_from_json(jitem); \
                add_##name##_to_room(list_idx, item_idx);          \
            }                                                      \
        }                                                          \
    } while (0)

bool load_rooms_from_json(void)
{
    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(ROOMS_JSON_FILEPATH, &sb)) return false;
    nob_sb_append_null(&sb);

    cJSON *jrooms = cJSON_Parse(sb.items);
    nob_sb_free(sb);
    if (!jrooms) {
        TraceLog(LOG_WARNING, "JSON parse error\n");
        return false;
    }

    reset_things();

    for (int i = 0; i < cJSON_GetArraySize(jrooms); i++) {
        cJSON *jroom = cJSON_GetArrayItem(jrooms, i);
        if (!jroom) {
            cJSON_Delete(jrooms);
            TraceLog(LOG_WARNING, "Couldn't get room json object %d\n", i);
            return false;
        }

        ThingIdx room_idx = new_room();
        if (game.current_room == NIL) {
            game.current_room = room_idx; // TODO: will this be always true?
        }

        cJSON *jblocks = cJSON_GetObjectItemCaseSensitive(jroom, "blocks");
        if (jblocks && cJSON_IsArray(jblocks)) {
            fill_list_from_json_array(jblocks, room_idx, block);
        } else {
            cJSON_Delete(jrooms);
            TraceLog(LOG_WARNING, "Couldn't parse blocks json in room %d\n", i);
            return false;
        }

        cJSON *jdoors = cJSON_GetObjectItemCaseSensitive(jroom, "doors");
        if (jdoors && cJSON_IsArray(jdoors)) {
            fill_list_from_json_array(jdoors, room_idx, door);
        } else {
            cJSON_Delete(jrooms);
            TraceLog(LOG_WARNING, "Couldn't parse doors json in room %d\n", i);
            return false;
        }
    }

    cJSON_Delete(jrooms);
    return true;
}

/// Collisions
static inline Rectangle rect_from_v2(Vector2 pos, Vector2 size)
{
    return (Rectangle){.x=pos.x, .y=pos.y, .width=size.x, .height=size.y};
}
static inline Rectangle rect(const Thing *t) { return rect_from_v2(t->pos, t->size); }
static inline Rectangle player_rect(void) { return rect(get_player()); }

/// Room Functions

// TODO: use enum, plese *eyeroll*
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
    ThingIdx block_idx = CURRENT_ROOM->first_block;
    while (block_idx != NIL) {
        const Thing *block = get_thing(block_idx);
        if (game.debug) {
            DrawRectangleLinesEx(rect(block), 1, BLACK);
            //const float sizex = block->size.x/5;
            //const float sizey = block->size.y/5;
            const float sizex = 1.f;
            const float sizey = 1.f;
            Rectangle top   = {block->pos.x, block->pos.y, block->size.x, sizey};
            Rectangle bot   = {block->pos.x, block->pos.y + block->size.y - sizey, block->size.x, sizey};
            Rectangle left  = {block->pos.x, block->pos.y, sizex, block->size.y};
            Rectangle right = {block->pos.x + block->size.x - sizex, block->pos.y, sizex, block->size.y};
            DrawRectangleRec(top, GREEN);
            DrawRectangleRec(bot, GREEN);
            DrawRectangleRec(left, GREEN);
            DrawRectangleRec(right, GREEN);
        } else {
            DrawRectangleRec(rect(block), BLACK);
        }

        block_idx = block->next_block;
    }

    // Doors
    ThingIdx door_idx = CURRENT_ROOM->first_door;
    size_t i = 0;
    while (door_idx != NIL) {
        const Thing *d = get_thing(door_idx);

        DrawRectangleRec(rect(d), RED);
        draw_arrow(d->pos.x + (d->spawn_left ? -20 : d->size.x + 10), d->pos.y+d->size.y/2-5, 10, RED, d->spawn_left ? 3 : 1);
        DrawText(TextFormat("%zu", i), d->pos.x + 10, d->pos.y + 10, 20, DARKBROWN);
        DrawText(TextFormat("> %lu", d->takes_to), d->pos.x + 10, d->pos.y + 30, 20, DARKBROWN);

        i++;
        door_idx = d->next_door;
    }
}
///

/// Player Functions
static inline Vector2 GetMousePositionRelativeToCamera(void) { return GetScreenToWorld2D(GetMousePosition(), game.camera); }

void player_init(void)
{
    Vector2 pos  = {200.f, 400.f};
    Vector2 size = {25.f, 50.f};
    Vector2 vel  = {PLAYER_VELOCITY, 0.f};

    game.player = new_player(pos, size, vel);
    Thing *player = get_player();
    
    player->pos  = (Vector2){200.f, 400.f};
    player->size = (Vector2){25.f, 50.f};
    player->vel  = (Vector2){PLAYER_VELOCITY, 0.f};

    game.camera.target = Vector2Add(player->pos, Vector2Scale(player->size, .5f));
}

void player_respawn(Vector2 checkpoint)
{
    get_player()->pos = checkpoint;
}

void player_draw(void)
{
    Thing *player = get_player();
    if (game.debug) DrawRectangleLines(player->pos.x, player->pos.y, player->size.x, player->size.y, DARKBLUE); 
    else DrawRectangleV(player->pos, player->size, DARKBLUE); 
}

Thing *player_check_door_collision()
{
    Thing *room = CURRENT_ROOM;
    ThingIdx door_idx = room->first_door;
    while (door_idx != NIL) { 
        Thing *d = get_thing(door_idx);
        if (CheckCollisionRecs(rect(d), player_rect())) return d;
        door_idx = d->next_door;
    }
    return THING_NIL;
}

void player_handle_controls()
{
    Thing *player = get_player();

    player->is_moving = false;
    player->direction = DIR_FRONT;

    if (IsKeyDown(KEY_A)) {
        player->is_moving = true;
        player->direction = DIR_LEFT;
    } else if (IsKeyDown(KEY_D)) {
        player->is_moving = true;
        player->direction = DIR_RIGHT;
    }

    if (IsKeyDown(KEY_S) && !player->is_grounded) {
        player->vel.y += GROUND_YOURSELF_FORCE;
    }

    if (IsKeyPressed(KEY_SPACE) && (player->is_grounded || player->coyote_timer > 0)) {
        player->is_grounded = false;
        player->coyote_timer = 0;
        player->vel.y = -JUMP_FORCE;
    }
}

void player_move_and_collide_y(float dt)
{
    Thing *player = get_player();

    player->pos.y += player->vel.y * dt;

    if (player->pos.y > game.screen_height + 500) {
        player_respawn((Vector2){200, 400});
        player->vel.y = 0;
        return;
    }

    player->is_grounded = false;

    Rectangle p_rect = player_rect();
    ThingIdx block_idx = CURRENT_ROOM->first_block;
    while (block_idx != NIL) {
        Thing *block = get_thing(block_idx);
        Rectangle b_rect = rect(block);

        if (!CheckCollisionRecs(p_rect, b_rect)) {
            block_idx = block->next_block;
            continue;
        }

        if (player->vel.y > 0) {
            player->pos.y = block->pos.y - player->size.y;
            player->vel.y = 0;
            player->is_grounded = true;
        } else if (player->vel.y < 0) {
            player->pos.y = block->pos.y + block->size.y;
            player->vel.y = 0;
        }

        block_idx = block->next_block;
    }

    if (player->is_grounded) {
        player->coyote_timer = COYOTE_TIMER;
    } else {
        player->coyote_timer -= dt;
        player->vel.y += game.gravity * dt;
    }
}

void player_move_and_collide_x(float dt)
{
    Thing *player = get_player();

    if (player->direction == DIR_FRONT) return;

    player->pos.x += player->vel.x * player->direction * dt;

    Rectangle p_rect = player_rect();
    ThingIdx block_idx = CURRENT_ROOM->first_block;
    while (block_idx != NIL) {
        Thing *block = get_thing(block_idx);
        Rectangle b_rect = rect(block);

        if (!CheckCollisionRecs(p_rect, b_rect)) {
            block_idx = block->next_block;
            continue;
        }

        if (player->direction == DIR_RIGHT)
            player->pos.x = block->pos.x - player->size.x;
        else if (player->direction == DIR_LEFT)
            player->pos.x = block->pos.x + block->size.x;

        block_idx = block->next_block;
    }
}

void player_check_move_through_door(void)
{
    Thing *player = get_player();

    Thing *d_from = player_check_door_collision();
    if (d_from == THING_NIL) return;

    RoomID new_room_id = d_from->takes_to;
    ThingIdx new_room_idx = get_room_by_id(new_room_id);
    Thing *new_room = get_thing(new_room_idx);

    ThingIdx d_to_idx = new_room->first_door;
    Thing *d_to;
    while (d_to_idx != NIL) {
        d_to = get_thing(d_to_idx);
        if (d_to->takes_to == CURRENT_ROOM->id) {
            break;
        }

        d_to_idx = d_to->next_door;
    }

    if (d_to_idx == NIL) {
        TraceLog(LOG_WARNING, "No door connection from room %lu to room %lu", CURRENT_ROOM->id, new_room->id);
        return;
    }

    game.current_room = new_room_idx;

    if (d_to->spawn_left)
        player->pos = (Vector2){d_to->pos.x - player->size.x - 10.f,  d_to->pos.y + d_to->size.y - player->size.y};
    else
        player->pos = (Vector2){d_to->pos.x + d_to->size.x  + 10.f,  d_to->pos.y + d_to->size.y - player->size.y};

    player->direction = d_to->spawn_left ? DIR_LEFT : DIR_RIGHT;
    player->is_moving = false;
    player->is_grounded = false;
    player->vel.y = 0.f;
    player->disabled_movements_timer = DISABLED_MOVEMENTS_TIMER;
}

void player_update(float dt)
{
    Thing *player = get_player();

    player_move_and_collide_y(dt);

    if (player->disabled_movements_timer > 0) {
        player->disabled_movements_timer -= dt;
        return;
    }

    if (game.state == PLAYING && !game.terminal.is_shown) player_handle_controls();
    player_move_and_collide_x(dt);
    player_check_move_through_door();
}

void camera_update(void)
{
    if (game.state == BUILDING) {
        float wheel = GetMouseWheelMove();
        if (fabsf(wheel) > 1e-6) game.camera.zoom = expf(logf(game.camera.zoom) + wheel*0.1f);
        if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) return;
        if (IsKeyDown(KEY_A)) game.camera.target.x -= 10;
        if (IsKeyDown(KEY_D)) game.camera.target.x += 10;
        if (IsKeyDown(KEY_W)) game.camera.target.y -= 10;
        if (IsKeyDown(KEY_S)) game.camera.target.y += 10;
    } else {
        Thing *player = get_player();
        game.camera.target = Vector2Add(player->pos, Vector2Scale(player->size, .5f));
    }
}
///

void check_unsaved_changes(void)
{
    if (game.is_dirty) {
        printf("\nWARNING: You have unsaved changes, wanna save? [Y/N]\n");
        int c;
        do {
            c = getchar();
        } while (c != 'Y' && c != 'N');
        if (c == 'Y') save_rooms_to_json();
    }
}

/// Game Initialization
void game_init(void)
{
    game.screen_width = GetScreenWidth();
    game.screen_height = GetScreenHeight();

    reset_things();

    if (!load_rooms_from_json()) {
        TraceLog(LOG_FATAL, "Could not load rooms from %s", ROOMS_JSON_FILEPATH);
        exit(1);
    }

    game.camera.offset = (Vector2){ game.screen_width/2.f, game.screen_height/2.f };
    game.camera.rotation = 0.f;
    game.camera.zoom = 1.f;

    game.gravity = 1200.f;

    player_init();

    da_push(&game.terminal, (String){0}); // First empty terminal
}
///

/// Modes
void building_mode(void)
{
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
        if (IsKeyPressed(KEY_S)) save_rooms_to_json();
        else if (IsKeyPressed(KEY_P) && CURRENT_ROOM->id+1 <= rooms_count) {
            game.current_room = get_room_by_id(CURRENT_ROOM->id + 1);
        } else if (IsKeyPressed(KEY_N) && CURRENT_ROOM->id > 1) {
            game.current_room = get_room_by_id(CURRENT_ROOM->id - 1);
        } else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 mouse = GetMousePositionRelativeToCamera();
            for (ThingIdx i = 1; i < game.things.count; i++) {
                Thing *t = get_thing(i);
                if (t->kind == KIND_NIL) continue;
                if (t->traits & TRAIT_NON_SELECTABLE) continue;
                if (CheckCollisionPointRec(mouse, rect(t))) {
                    da_push(&game.selected_things, i);
                    game.selected_things.active_index = game.selected_things.count-1;
                }
            }
        } else if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            da_clear(&game.selected_things);
        }
    } else if (game.selected_things.count > 0) {
        Vector2 mouse = GetMousePositionRelativeToCamera();
        bool mouse_cursor_changed = false;
        for (size_t i = 0; i < game.selected_things.count; i++) {
            ThingIdx idx = game.selected_things.items[i];
            Thing *t = get_thing(idx);

            if (CheckCollisionPointRec(mouse, rect(t))) {
                if (IsKeyDown(KEY_LEFT_SHIFT)) {
                    SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);
                    mouse_cursor_changed = true;
                    game.selected_things.resizing = true;
                } else {
                    SetMouseCursor(MOUSE_CURSOR_RESIZE_ALL);
                    mouse_cursor_changed = true;
                    game.selected_things.dragging = true;
                }
            } else if (!mouse_cursor_changed) {
                SetMouseCursor(MOUSE_CURSOR_DEFAULT);
            }

            if (game.selected_things.dragging) {
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                    Vector2 delta = Vector2Scale(GetMouseDelta(), 1.0f / game.camera.zoom);
                    t->pos = Vector2Add(t->pos, delta);
                } else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) game.selected_things.dragging = false;
            } else if (game.selected_things.resizing) {
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && IsKeyDown(KEY_LEFT_SHIFT)) {
                    // TODO: prevent the object from vanishing from reality by setting a minimum size and a position boundary
                    Vector2 delta = Vector2Scale(GetMouseDelta(), 1.0f / game.camera.zoom);
                    t->size = Vector2Add(t->size, delta);
                } else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) game.selected_things.resizing = false;
            } else if (IsKeyDown(KEY_LEFT_SHIFT)) {
                // TODO: prevent the object from vanishing from reality by setting a minimum size and a position boundary
                if (IsKeyPressed(KEY_UP))         t->size.y -= 1;
                else if (IsKeyPressed(KEY_RIGHT)) t->size.x += 1;
                else if (IsKeyPressed(KEY_DOWN))  t->size.y += 1;
                else if (IsKeyPressed(KEY_LEFT))  t->size.x -= 1;
            } else {
                if (IsKeyPressed(KEY_UP))         t->pos.y -= 1;
                else if (IsKeyPressed(KEY_RIGHT)) t->pos.x += 1;
                else if (IsKeyPressed(KEY_DOWN))  t->pos.y += 1;
                else if (IsKeyPressed(KEY_LEFT))  t->pos.x -= 1;
            }
        }

        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_D)) { // TODO: maybe a way to undo it?
            TraceLog(LOG_WARNING, "Implement thing deletion");
            //bool can_delete = true;
            //for (size_t i = 0; i < game.selected_things.count; i++) {
            //    ThingIdx idx = game.selected_things.items[i];
            //    Thing *t = get_thing(idx);
            //    if (t->trait & TRAIT_NON_DELETABLE) {
            //        can_delete = false;
            //        break;
            //    }
            //}
            //if (can_delete) {
            //    int i = game.selected_things.count - 1;
            //    while (i > 0) {
            //        
            //    }
            //}
        }
    }
}

void update(float dt)
{
    if (!game.is_paused) player_update(dt);
    camera_update();
}

void draw(void)
{
    room_draw();
    player_draw();

    if (game.state == BUILDING) {
        if (game.selected_things.count > 0) {
            for (size_t i = 0; i < game.selected_things.count; i++) {
                ThingIdx idx = game.selected_things.items[i];
                Thing *t = get_thing(idx);

                DrawRectangleLinesEx(rect(t), 3, BLACK);

                Color color = i == game.selected_things.active_index ? RED : ORANGE;
                DrawRectangleLinesEx((Rectangle){t->pos.x+1, t->pos.y+1, t->size.x-2, t->size.y-2}, 1, color);
            }
        }
    }
}

void ui(void)
{
    if (game.is_paused) DrawText("PAUSE", game.screen_width-100, 10, 20, BLACK);
    if (game.debug) {
        DrawText("DEBUG", game.screen_width-100, 30, 20, BLACK);

        size_t line = 1;
        DrawText(TextFormat("FPS: %d", GetFPS()), 25, 25*line++, 20, BLACK);

        Vector2 mouse = GetMousePosition();
        DrawText(TextFormat("mouse abs: (%.2f, %.2f)", mouse.x, mouse.y), 25, 25*line++, 20, BLACK);

        mouse = GetMousePositionRelativeToCamera();
        DrawText(TextFormat("mouse rel: (%.2f, %.2f)", mouse.x, mouse.y), 25, 25*line++, 20, BLACK);
    }
    if (game.state == BUILDING) {
        DrawText("BUILDING", game.screen_width-100, 50, 20, BLACK);

        if (game.selected_things.count > 0) {
            DrawRectangle(0, 0, 300, 600, BLACK);
            Thing *t = get_active_thing();
            float y_spacing = 2;
            float divider = 10*y_spacing;
            float start_x = y_spacing;
            float line_y = 2;
            float font_kind = 30;
            float font_cathegory = 25;
            float font_property = 20;

            DrawText(thing_kind_to_string(t->kind), start_x, line_y, font_kind, WHITE);
            line_y += font_kind + y_spacing + divider;
            {
                DrawText("  Position", start_x, line_y, font_cathegory, WHITE);
                line_y += font_cathegory + y_spacing;

                DrawText(TextFormat("    x: %f", t->pos.x), start_x, line_y, font_property, WHITE);
                line_y += font_property + y_spacing;

                DrawText(TextFormat("    y: %f", t->pos.y), start_x, line_y, font_property, WHITE);
                line_y += font_property + y_spacing;
            }
            line_y += divider;
            {
                DrawText("  Size", start_x, line_y, font_cathegory, WHITE);
                line_y += font_cathegory + y_spacing;

                DrawText(TextFormat("    x: %f", t->size.x), start_x, line_y, font_property, WHITE);
                line_y += font_property + y_spacing;

                DrawText(TextFormat("    y: %f", t->size.y), start_x, line_y, font_property, WHITE);
                line_y += font_property + y_spacing;
            }
            line_y += divider;

            switch (t->kind) {
            case KIND_PLAYER: { } break;

            case KIND_BLOCK: { } break;

            case KIND_DOOR:
            {
                DrawText("  Specific", start_x, line_y, font_cathegory, WHITE);
                line_y += font_cathegory + y_spacing;

                DrawText(TextFormat("    takes to: %lu", t->takes_to), start_x, line_y, font_property, WHITE);
                line_y += font_property + y_spacing;

                DrawText(TextFormat("    spawn left: %s", t->spawn_left ? "true" : "false"), start_x, line_y, font_property, WHITE);
                line_y += font_property + y_spacing;
            } break;

            case KIND_ROOM:
            case KIND_NIL:
            case __kinds_count:
            default:
                TraceLog(LOG_FATAL, "Unreachable thing kind %d drawing selected thing panel", t->kind);
                exit(1);
            }
        }
    }

    DrawText(TextFormat("Room %zu", CURRENT_ROOM->id), game.screen_width-100, game.screen_height-25, 20, BLACK);

    if (game.terminal.is_shown) {
        const float  TERM_SIZE_X = 300.0f;
        const float  TERM_SIZE_Y = 300.0f;
        const size_t TERM_LINES  = 10;
        const float  TERM_LINE_HEIGHT = TERM_SIZE_Y/TERM_LINES;
        Vector2 term_size = {TERM_SIZE_X, TERM_SIZE_Y};
        Vector2 term_pos = {game.screen_width-term_size.x, 0};
        DrawRectangleV(term_pos, term_size, BLACK);
        for (size_t i = 1; i < TERM_LINES; i++) {
            float y = term_pos.y+(TERM_LINE_HEIGHT)*i;
            DrawLineV((Vector2){term_pos.x, y}, (Vector2){term_pos.x+term_size.x, y}, WHITE);
        }

        const size_t Y_SPACING = 2;
        const size_t X_SPACING = 4;
        const size_t FONT_SIZE = TERM_LINE_HEIGHT - 2*Y_SPACING;
        const float FONT_SPACING = 5.0f;
        const size_t SIZE = game.terminal.count > TERM_LINES ? TERM_LINES : game.terminal.count;
        for (size_t i = 0; i < TERM_LINES; i++) {
            if (game.terminal.count <= i) break;
            Vector2 line_pos = {term_pos.x + X_SPACING, term_pos.y + Y_SPACING + (TERM_LINE_HEIGHT)*(TERM_LINES-i-1)};
            String line = game.terminal.items[SIZE-i-1];
            if (line.count >= 64) {
                DrawTextEx(GetFontDefault(), "TOO LONG", line_pos, FONT_SIZE, FONT_SPACING, GREEN);
            } else if (line.count > 0) {
                DrawTextEx(GetFontDefault(), TextFormat(S_FMT, S_ARG(line)), line_pos, FONT_SIZE, FONT_SPACING, GREEN);
            }
        }
    }
}

int atoin(const char *str, size_t len) {
    int res = 0;
    int sign = 1;
    size_t i = 0;

    if (len == 0 || str == NULL) return 0;

    if (str[0] == '-') {
        sign = -1;
        i++;
    } else if (str[0] == '+') {
        i++;
    }

    for (; i < len; i++) {
        if (str[i] < '0' || str[i] > '9') {
            break;
        }
        res = res * 10 + (str[i] - '0');
    }

    return res * sign;
}

void execute_terminal_command(void)
{
    StringView line = sv_from_s(*da_get_last(game.terminal));
    TraceLog(LOG_INFO, "Execute terminal command \""SV_FMT"\"", SV_ARG(line));
    da_push(&game.terminal, (String){0});

    line = sv_trim(line);
    if (line.count == 0) {
        TraceLog(LOG_WARNING, "Empty command");
        return;
    }

    StringView command = sv_chop_by_char(&line, ' ');
    line = sv_trim_left(line);

    if (sv_eq_cstr(command, "quit")) {
        check_unsaved_changes();
        exit(0);
    } else if (sv_eq_cstr(command, "set")) {
        if (game.selected_things.count == 0) {
            TraceLog(LOG_WARNING, "No things selected");
            return;
        }
        Thing *t = get_active_thing();
        StringView property = sv_chop_by_char(&line, ' ');
        line = sv_trim_left(line);
        StringView value = line;

        switch (t->kind) {
        case KIND_PLAYER:
        {
            TraceLog(LOG_WARNING, "TODO: set properties of \"%s\"", thing_kind_to_string(t->kind));
            return;
        } break;
        case KIND_BLOCK:
        {
            TraceLog(LOG_WARNING, "TODO: set properties of \"%s\"", thing_kind_to_string(t->kind));
            return;
        } break;
        case KIND_DOOR:
        {
            if (sv_eq_cstr(property, "takes_to")) {
                int v = atoin(value.data, value.count);
                if (v <= 0) {
                    TraceLog(LOG_WARNING, "Invalid positive integer value '%d' for property "SV_FMT, v,
                            SV_ARG(property));
                    return;
                }
                t->takes_to = v;
            } else if (sv_eq_cstr(property, "spawn_left")) {
                if (sv_eq_cstr(value, "true")) {
                    t->spawn_left = true;
                } else if (sv_eq_cstr(value, "false")) {
                    t->spawn_left = false;
                } else {
                    TraceLog(LOG_WARNING, "Invalid boolean value \""SV_FMT"\" for property "SV_FMT, SV_ARG(value),
                            SV_ARG(property));
                    return;
                }
            } else {
                TraceLog(LOG_WARNING, "Unknown property \""SV_FMT"\" for %s", SV_ARG(property), thing_kind_to_string(t->kind));
                return;
            }
        } break;
        case KIND_ROOM:
        {
            TraceLog(LOG_WARNING, "TODO: set properties of \"%s\"", thing_kind_to_string(t->kind));
            return;
        } break;

        case KIND_NIL:
        case __kinds_count:
        default:
            TraceLog(LOG_FATAL, "Unreachable thing kind %d setting thing property", t->kind);
            exit(1);
        }
    } else if (sv_eq_cstr(command, "add")) {
        StringView kind = sv_chop_by_char(&line, ' ');

        if (sv_eq_cstr(kind, "room")) {
            ThingIdx room_idx = new_room();
            game.current_room = room_idx;
        } else if (sv_eq_cstr(kind, "block")) {
            const Vector2 default_size = {50, 100};
            ThingIdx block_idx = new_block(
                GetMousePositionRelativeToCamera(),
                default_size
            );
            add_block_to_room(game.current_room, block_idx);
        } else if (sv_eq_cstr(kind, "door")) {
            const Vector2 default_size = {50, 100};
            const RoomID default_takes_to = NO_ID;
            const bool default_spawn_left = false;
            ThingIdx door_idx = new_door(
                GetMousePositionRelativeToCamera(),
                default_size,
                default_takes_to,
                default_spawn_left
            );
            add_block_to_room(game.current_room, door_idx);
        } else {
            TraceLog(LOG_WARNING, "Unknown kind \""SV_FMT"\"", SV_ARG(kind));
            return;
        }
    } else {
        TraceLog(LOG_WARNING, "Unknown command \""SV_FMT"\"", SV_ARG(command));
        return;
    }
}

int main(void)
{
    InitWindow(800, 600, "Placeholder");
    SetWindowState(/*FLAG_FULLSCREEN_MODE | */FLAG_WINDOW_RESIZABLE);
    SetTargetFPS(60);

    game_init();

    float dt;

    while (!WindowShouldClose()) {

        dt = GetFrameTime();

        if (game.terminal.is_shown) {
            String *line = NULL;
            switch (GetKeyPressed()) {
            case KEY_ENTER:
                execute_terminal_command();
                break;
            case KEY_BACKSPACE:
                line = da_get_last(game.terminal);
                if (line->count > 0) s_pop(line);
                break;
            default:
                char c = GetCharPressed();
                while (c) {
                    String *line = da_get_last(game.terminal);
                    s_push(line, c);
                    c = GetCharPressed();
                }
            }
        } else {
            if (IsKeyPressed(KEY_RIGHT_CONTROL) && IsKeyPressed(KEY_D)) game.debug = !game.debug;
            if (!(IsKeyDown(KEY_LEFT_CONTROL)) && IsKeyPressed(KEY_P)) game.is_paused = !game.is_paused;
            if (IsKeyPressed(KEY_B)) {
                if (game.state == BUILDING) {
                    game.camera.zoom = 1.f;
                    game.state = PLAYING;
                    da_clear(&game.selected_things);
                } else if (game.state == PLAYING) game.state = BUILDING;
            }

            if (game.state == BUILDING) {
                building_mode();
            }
        }

        if (IsKeyDown(KEY_RIGHT_CONTROL) && IsKeyPressed(KEY_T)) {
            game.terminal.is_shown = !game.terminal.is_shown;
        }

        update(dt);

        BeginDrawing();
        ClearBackground(GRAY);

        BeginMode2D(game.camera);

        draw();

        EndMode2D();

        ui();

        EndDrawing();
    }

    CloseWindow();
    check_unsaved_changes();
    return 0;
}
