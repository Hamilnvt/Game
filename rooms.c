void load_rooms(void)
{
    Room r;

    // Room 0
    {
        r = (Room){0};
        // Grounds
        da_push(&r.grounds, ((Ground){{-248.000000,543.000000},{1600.000000,60.000000}}));
        da_push(&r.grounds, ((Ground){{259.946808,405.086060},{171.000000,24.000000}}));
        da_push(&r.grounds, ((Ground){{-165.500000,255.099976},{472.500000,16.099976}}));
        da_push(&r.grounds, ((Ground){{-60.500000,399.099976},{187.000000,32.000000}}));
        da_fit(&r.grounds);
        // Walls
        da_push(&r.walls, ((Wall){{-247.570129,113.217987},{57.000000,488.000000}}));
        da_fit(&r.walls);
        // Doors
        da_push(&r.doors, ((Door){{429.500000,309.904541},{50.000000,100.000000}, 1, true}));
        da_fit(&r.doors);

        da_push(&rooms, r);
    }

    // Room 1
    {
        r = (Room){0};
        // Grounds
        da_push(&r.grounds, ((Ground){{100.000000,550.000000},{1600.000000,60.000000}}));
        da_push(&r.grounds, ((Ground){{349.897278,399.380280},{100.000000,25.000000}}));
        da_fit(&r.grounds);
        // Doors
        da_push(&r.doors, ((Door){{300.000000,300.000000},{50.000000,100.000000}, 0, false}));
        da_fit(&r.doors);

        da_push(&rooms, r);
    }

    // Room 2
    {
        r = (Room){0};
        // Grounds
        da_push(&r.grounds, ((Ground){{100.000000,550.000000},{1600.000000,60.000000}}));
        da_push(&r.grounds, ((Ground){{144.515564,409.804535},{100.000000,25.000000}}));
        da_fit(&r.grounds);
        // Doors
        da_push(&r.doors, ((Door){{300.000000,300.000000},{50.000000,100.000000}, 0, true}));
        da_fit(&r.doors);

        da_push(&rooms, r);
    }

    // Room 3
    {
        r = (Room){0};
        // Grounds
        da_push(&r.grounds, ((Ground){{170.026901,553.149902},{100.000000,50.000000}}));
        da_fit(&r.grounds);
        // Doors
        da_push(&r.doors, ((Door){{120.260849,452.713013},{50.000000,100.000000}, 0, false}));
        da_fit(&r.doors);

        da_push(&rooms, r);
    }

    da_fit(&rooms);

}
