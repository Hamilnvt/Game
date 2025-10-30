void load_rooms(void)
{
    TraceLog(LOG_INFO, "In load rooms");
    Room r;

    // Room 0
    {
        r = (Room){0};
        // Grounds
        da_push(&r.grounds, ((Ground){{100.000000,550.000000},{1600.000000,60.000000}}));
        da_push(&r.grounds, ((Ground){{144.515564,409.804535},{100.000000,25.000000}}));
        da_fit(&r.grounds);
        // Doors
        da_push(&r.doors, ((Door){{300.000000,300.000000},{50.000000,100.000000}, 1}));
        da_fit(&r.doors);

        da_push(&rooms, r);
    }

    // Room 1
    {
        r = (Room){0};
        // Grounds
        da_push(&r.grounds, ((Ground){{0.000000,0.000000},{0.000000,0.000000}}));
        da_push(&r.grounds, ((Ground){{0.000000,0.000000},{-70379264.000000,0.000000}}));
        da_fit(&r.grounds);
        // Doors
        da_push(&r.doors, ((Door){{-0.000000,0.000000},{-23723638130178979660824576.000000,0.000000}, 13037993995}));
        da_fit(&r.doors);

        da_push(&rooms, r);
    }

    da_fit(&rooms);

    da_free(&r.grounds);
    da_free(&r.walls);
    da_free(&r.doors);
}
