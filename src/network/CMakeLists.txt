add_library(network STATIC
    packet.cpp
    packet.h
    room.cpp
    room.h
    room_member.cpp
    room_member.h
)

create_target_directory_groups(network)

target_link_libraries(network PRIVATE common enet asls)
