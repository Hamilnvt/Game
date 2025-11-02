void load_rooms(void)
{
    Room r;

    // Room 0
    {
        r = (Room){0};
        // Grounds
        da_push(&r.grounds, ((Ground){{100.000000,550.000000},{1600.000000,60.000000}}));
        da_push(&r.grounds, ((Ground){{309.946808,415.086060},{100.000000,25.000000}}));
        da_push(&r.grounds, ((Ground){{48.500000,360.099976},{100.000000,50.000000}}));
        da_push(&r.grounds, ((Ground){{-139.539001,242.199600},{100.000000,50.000000}}));
        da_push(&r.grounds, ((Ground){{-191.041611,549.343384},{100.000000,50.000000}}));
        da_push(&r.grounds, ((Ground){{-95.128845,550.248230},{100.000000,50.000000}}));
        da_push(&r.grounds, ((Ground){{0.783905,550.248230},{100.000000,50.000000}}));
        da_fit(&r.grounds);
        // Walls
        da_push(&r.walls, ((Wall){{-342.655670,185.630295},{50.000000,100.000000}}));
        da_push(&r.walls, ((Wall){{-241.759705,184.494354},{50.000000,100.000000}}));
        da_push(&r.walls, ((Wall){{-241.092224,282.811462},{50.000000,100.000000}}));
        da_push(&r.walls, ((Wall){{-240.268173,377.572632},{50.000000,100.000000}}));
        da_push(&r.walls, ((Wall){{-240.828140,476.873047},{50.000000,100.000000}}));
        da_push(&r.walls, ((Wall){{-240.221603,576.344116},{50.000000,100.000000}}));
        da_fit(&r.walls);
        // Doors
        da_push(&r.doors, ((Door){{409.500000,315.904541},{50.000000,100.000000}, 1, true}));
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
