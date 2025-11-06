void load_rooms(void)
{
    Room r;

    // Room 0
    {
        r = (Room){0};
        // Blocks
        da_push(&r.blocks, ((Block){{-925.000000,546.000000},{1537.000000,58.000000}}));
        da_push(&r.blocks, ((Block){{229.946808,433.086060},{178.000000,29.000000}}));
        da_push(&r.blocks, ((Block){{-82.500000,73.099976},{472.500000,16.099976}}));
        da_push(&r.blocks, ((Block){{-111.500000,341.099976},{187.000000,32.000000}}));
        da_push(&r.blocks, ((Block){{-421.570129,365.217987},{161.000000,237.000000}}));
        da_push(&r.blocks, ((Block){{-16.500000,445.099976},{156.000000,104.000000}}));
        da_fit(&r.blocks);
        // Doors
        da_push(&r.doors, ((Door){{429.500000,309.904541},{50.000000,100.000000}, 1, true}));
        da_fit(&r.doors);

        da_push(&rooms, r);
    }

    // Room 1
    {
        r = (Room){0};
        // Blocks
        da_push(&r.blocks, ((Block){{100.000000,550.000000},{1600.000000,60.000000}}));
        da_push(&r.blocks, ((Block){{355.897278,444.380280},{100.000000,25.000000}}));
        da_fit(&r.blocks);
        // Doors
        da_push(&r.doors, ((Door){{300.000000,300.000000},{50.000000,100.000000}, 0, false}));
        da_fit(&r.doors);

        da_push(&rooms, r);
    }

    // Room 2
    {
        r = (Room){0};
        // Blocks
        da_push(&r.blocks, ((Block){{100.000000,550.000000},{1600.000000,60.000000}}));
        da_push(&r.blocks, ((Block){{144.515564,409.804535},{100.000000,25.000000}}));
        da_fit(&r.blocks);
        // Doors
        da_push(&r.doors, ((Door){{300.000000,300.000000},{50.000000,100.000000}, 0, true}));
        da_fit(&r.doors);

        da_push(&rooms, r);
    }

    // Room 3
    {
        r = (Room){0};
        // Blocks
        da_push(&r.blocks, ((Block){{170.026901,553.149902},{100.000000,50.000000}}));
        da_fit(&r.blocks);
        // Doors
        da_push(&r.doors, ((Door){{120.260849,452.713013},{50.000000,100.000000}, 0, false}));
        da_fit(&r.doors);

        da_push(&rooms, r);
    }

    da_fit(&rooms);

}
