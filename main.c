#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "raylib.h"
#include "raymath.h"
#include "thirdparties/cJSON/cJSON.h"
#include "dynamic_arrays.h"

#define NOB_IMPLEMENTATION
#include "nob.h"

/// Macros
#define UNUSED(x) (void)(x)

#define NOT_IMPLEMENTED(msg)                    \
    do {                                        \
        printf("NOT IMPLEMENTED: %s\n", (msg)); \
        exit(1);                                \
    } while (0)

#define JUMP_FORCE 500
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
    switch(kind) {
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

typedef enum
{
    DIR_LEFT = -1,
    DIR_FRONT = 0,
    DIR_RIGHT = 1
} MoveDirection;

typedef enum
{
    TRAIT_NONE       = 0,
    TRAIT_DRAGGABLE  = (1 << 0),
    TRAIT_RESIZEABLE = (1 << 1)
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
    // TODO: keep used list
    Thing *items;
    size_t count;
    size_t capacity;
} Things;

typedef enum
{
    PLAYING,
    BUILDING
} GameState;

typedef struct
{
    ThingIdx *items;
    size_t count;
    size_t capacity;

    bool dragging;
    bool resizing;
} SelectedThings;

typedef struct
{
    GameState state; // PLAYING
    bool is_dirty;
    bool debug;
    bool is_paused;

    SelectedThings selected_things;
    bool adding_thing;

    Things things;
    size_t screen_width;
    size_t screen_height;
    Camera2D camera;
    ThingIdx player;
    ThingIdx current_room;
    float gravity;
} Game;
static Game game = {0};

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
        .traits     = TRAIT_DRAGGABLE,
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

ThingIdx new_room(RoomID id)
{
    ThingIdx i = alloc_thing();
    if (i == NIL) {
        TraceLog(LOG_ERROR, "Could not allocate %s", thing_kind_to_string(KIND_ROOM));
        abort();
    }

    game.things.items[i] = (Thing){
        .kind = KIND_ROOM,
        .id   = id
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

#define COYOTE_TIMER 0.15f
#define DISABLED_MOVEMENTS_TIMER 0.5f

///

/// Rooms
static size_t current_room = 0;
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
        cJSON_AddItemToObject(jroom, "blocks", doors);
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

        ThingIdx room_idx = new_room(i);
        assert(room_idx != NIL);
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
//static inline Rectangle selected_things_rect(void) { return rect_from_v2(*game.selected_things.pos, *game.selected_things.size); }

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
        char buffer[32] = {0};
        sprintf(buffer, "%zu", i);
        DrawText(buffer, d->pos.x + 10, d->pos.y + 10, 20, DARKBROWN);
        memset(buffer, 0, sizeof(buffer));
        sprintf(buffer, "> %lu", d->takes_to);
        DrawText(buffer, d->pos.x + 10, d->pos.y + 30, 20, DARKBROWN);

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
    assert(game.player != NIL);
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

    // TODO: make it jump more if holding space bar
    if (IsKeyPressed(KEY_SPACE) && (player->is_grounded || player->coyote_timer > 0)) {
        // TODO: handle all the collided_blocks and their directions here
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
        TraceLog(LOG_WARNING, "No door connection from %zu to %zu", CURRENT_ROOM->id, new_room->id);
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

    if (game.state == PLAYING) player_handle_controls();
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
        int c = getchar();
        while (c != 'Y' && c != 'N') {
            c = getchar();
        }
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
}
///

/// Modes
//void building_mode(void)
//{
//    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_A)) {
//        game.adding_thing = !game.adding_thing;
//    } else if (game.adding_thing) {
//        switch (GetKeyPressed())
//        {
//        case KEY_ONE: {
//            Block b = {0};
//            b.pos = GetMousePositionRelativeToCamera();
//            b.size = (Vector2){50, 100};
//            da_push(&CURRENT_ROOM->blocks, b); 
//        } break;
//        case KEY_TWO: {
//            // TODO: use new_door
//            Door d = {0};
//            d.pos = GetMousePositionRelativeToCamera();
//            d.size = (Vector2){50, 100};
//            da_push(&CURRENT_ROOM->doors, d); 
//            // TODO: add mechanism to change:
//            // - spawning direction
//            // - room connection
//            // >> sarebbe carino che quando si seleziona un oggetto si possono modificare i suoi valori
//        } break;
//        case KEY_THREE: {
//            current_room = rooms.count;
//            da_push(&rooms, ((Room){0}));
//        } break;
//        }
//    } else if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
//        if (IsKeyPressed(KEY_S)) save_rooms_to_json();
//        else if (IsKeyPressed(KEY_P) && current_room+1 < rooms.count) current_room++;
//        else if (IsKeyPressed(KEY_N) && current_room > 0) current_room--;
//        else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
//            game.selected_things.active = false;
//            Vector2 mouse = GetMousePositionRelativeToCamera();
//            if (CheckCollisionPointRec(mouse, player_rect())) {
//                game.selected_things.active = true;
//                game.selected_things.type = OBJ_PLAYER;
//                game.selected_things.pos = &player.pos;
//                game.selected_things.size = &player.size;
//            } else {
//
//                for (size_t i = 0; i < CURRENT_ROOM->blocks.count; i++) {
//                    Block *b = &CURRENT_ROOM->blocks.items[i];
//                    if (CheckCollisionPointRec(mouse, rect(b))) {
//                        game.selected_things.active = true;
//                        game.selected_things.type = OBJ_BLOCK;
//                        game.selected_things.pos = &b->pos;
//                        game.selected_things.size = &b->size;
//                        game.selected_things.index = i;
//                        break;
//                    }
//                }
//
//                if (!game.selected_things.active) {
//                    for (size_t i = 0; i < CURRENT_ROOM->doors.count; i++) {
//                        Door *d = &CURRENT_ROOM->doors.items[i];
//                        if (CheckCollisionPointRec(mouse, rect(d))) {
//                            game.selected_things.active = true;
//                            game.selected_things.type = OBJ_DOOR;
//                            game.selected_things.pos = &d->pos;
//                            game.selected_things.size = &d->size;
//                            game.selected_things.index = i;
//                            break;
//                        }
//                    }
//                }
//
//            }
//        }
//    } else if (game.selected_things.active) {
//        // TODO: ovviamente non funziona
//        Vector2 mouse = GetMousePositionRelativeToCamera();
//        if (game.selected_things.dragging) {
//            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
//                Vector2 delta = GetMouseDelta();
//                delta = Vector2Scale(delta, 1.0f / game.camera.zoom);
//                *game.selected_things.pos = Vector2Add(*game.selected_things.pos, delta);
//            } else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) game.selected_things.dragging = false;
//        } else if (game.selected_things.resizing) {
//            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && IsKeyDown(KEY_LEFT_SHIFT)) {
//                // TODO: prevent the object from vanishing from reality by setting a minimum size and a position boundary
//                Vector2 delta = GetMouseDelta();
//                delta = Vector2Scale(delta, 1.0f / game.camera.zoom);
//                *game.selected_things.size = Vector2Add(*game.selected_things.size, delta);
//            } else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) game.selected_things.dragging = false;
//        }
//        if (IsKeyDown(KEY_LEFT_SHIFT)) {
//            // TODO: prevent the object from vanishing from reality by setting a minimum size and a position boundary
//            if (IsKeyPressed(KEY_UP))         game.selected_things.size->y -= 1;
//            else if (IsKeyPressed(KEY_RIGHT)) game.selected_things.size->x += 1;
//            else if (IsKeyPressed(KEY_DOWN))  game.selected_things.size->y += 1;
//            else if (IsKeyPressed(KEY_LEFT))  game.selected_things.size->x -= 1;
//        } else {
//            if (IsKeyPressed(KEY_UP))    game.selected_things.pos->y -= 1;
//            else if (IsKeyPressed(KEY_RIGHT)) game.selected_things.pos->x += 1;
//            else if (IsKeyPressed(KEY_DOWN))  game.selected_things.pos->y += 1;
//            else if (IsKeyPressed(KEY_LEFT))  game.selected_things.pos->x -= 1;
//        }
//
//        if (CheckCollisionPointRec(mouse, game.selected_things_rect())) {
//            if (IsKeyDown(KEY_LEFT_SHIFT)) {
//                SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);
//                game.selected_things.resizing = true;
//            } else {
//                SetMouseCursor(MOUSE_CURSOR_RESIZE_ALL);
//                game.selected_things.dragging = true;
//            }
//        } else {
//            SetMouseCursor(MOUSE_CURSOR_DEFAULT);
//        }
//        if (IsKeyPressed(KEY_D)) { // TODO: maybe a way to undo it?
//            switch (game.selected_things.type) {
//            case OBJ_BLOCK: {
//                da_remove(&CURRENT_ROOM->blocks, game.selected_things.index);
//                game.selected_things.active = false;
//            } break;
//            case OBJ_DOOR: {
//                da_remove(&CURRENT_ROOM->doors, game.selected_things.index);
//                game.selected_things.active = false;
//            } break;
//            default: TraceLog(LOG_INFO, "Cannot delete things of kind `%s`", ObjectTypeToString(game.selected_things.type));
//            }
//        }
//    }
//}
///

/// Main
int main(void)
{
    InitWindow(800, 600, "Placeholder");
    SetWindowState(/*FLAG_FULLSCREEN_MODE | */FLAG_WINDOW_RESIZABLE);
    SetTargetFPS(60);

    game_init();

    float dt;

    while (!WindowShouldClose()) {

        dt = GetFrameTime();

        if (!game.adding_thing && IsKeyPressed(KEY_D) && IsKeyPressed(KEY_RIGHT_CONTROL)) game.debug = !game.debug;
        if (!(IsKeyDown(KEY_LEFT_CONTROL)) && IsKeyPressed(KEY_P)) game.is_paused = !game.is_paused;
        if (IsKeyPressed(KEY_B)) {
            if (game.state == BUILDING) {
                // TODO
                //game.camera.zoom = 1.f;
                //game.state = PLAYING;
                //game.selected_things.active = false;
            } else if (game.state == PLAYING) game.state = BUILDING;
        }

        if (game.state == BUILDING) {
            // TODO
            //building_mode();
        }

        if (!game.is_paused) player_update(dt);
        camera_update();

        BeginDrawing();
        ClearBackground(GRAY);

        BeginMode2D(game.camera);

        room_draw();
        player_draw();

        if (game.state == BUILDING) {
            // TODO
            //if (game.selected_things.active) {
            //    DrawRectangleLinesEx((Rectangle){game.selected_things.pos->x, game.selected_things.pos->y, game.selected_things.size->x, game.selected_things.size->y}, 3, BLACK);
            //    DrawRectangleLinesEx((Rectangle){game.selected_things.pos->x+1, game.selected_things.pos->y+1, game.selected_things.size->x-2, game.selected_things.size->y-2}, 1, RED);
            //}
        }

        EndMode2D();

        if (game.is_paused) DrawText("PAUSE", game.screen_width-100, 10, 20, BLACK);
        if (game.debug) {
            DrawText("DEBUG", game.screen_width-100, 30, 20, BLACK);

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
        if (game.state == BUILDING) {
            DrawText("BUILDING", game.screen_width-100, 50, 20, BLACK);
            if (game.adding_thing) {
                size_t line = 1;
                size_t inv_line = 3;
                DrawText(TextFormat("room  (%zu)", inv_line--), 10, game.screen_height - 25*line++, 20, BLACK);
                DrawText(TextFormat("door  (%zu)", inv_line--), 10, game.screen_height - 25*line++, 20, BLACK);
                DrawText(TextFormat("block (%zu)", inv_line--), 10, game.screen_height - 25*line++, 20, BLACK);
                DrawText("ADD",        10, game.screen_height - 25*line++, 20, BLACK);
            }
        }

        char buffer[64];
        sprintf(buffer, "Room %zu", current_room);
        DrawText(buffer, game.screen_width-100, game.screen_height-25, 20, BLACK);

        EndDrawing();
    }

    CloseWindow();
    check_unsaved_changes();
    return 0;
}
