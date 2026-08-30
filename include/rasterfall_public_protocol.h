#ifndef RASTERFALL_PUBLIC_PROTOCOL_H
#define RASTERFALL_PUBLIC_PROTOCOL_H

/* Shared by the game and the public-room coordinator.  A room number is the
 * sole source of truth for transport selection; neither endpoint may fall
 * back to the other transport at runtime. */
#define RASTERFALL_PUBLIC_ROOM_MIN 0
#define RASTERFALL_PUBLIC_ROOM_MAX 9999
#define RASTERFALL_PUBLIC_RELAY_ROOM_MIN 5000
#define RASTERFALL_PUBLIC_PORT 28461

#define RASTERFALL_PUBLIC_VERSION 3
#define RASTERFALL_PUBLIC_REGISTER 1
#define RASTERFALL_PUBLIC_MATCH 2
#define RASTERFALL_PUBLIC_PROBE 3
#define RASTERFALL_PUBLIC_ERROR 4
#define RASTERFALL_PUBLIC_REGISTERED 5

#define RASTERFALL_PUBLIC_ROLE_HOST 1
#define RASTERFALL_PUBLIC_ROLE_GUEST 2

#define RASTERFALL_PUBLIC_ERROR_ROOM_EXISTS 1
#define RASTERFALL_PUBLIC_ERROR_ROOM_NOT_FOUND 2
#define RASTERFALL_PUBLIC_ERROR_ROOM_FULL 3
#define RASTERFALL_PUBLIC_ERROR_SERVER_FULL 4
#define RASTERFALL_PUBLIC_ERROR_PROTOCOL 5

#define RASTERFALL_PUBLIC_REGISTER_SIZE 12
#define RASTERFALL_PUBLIC_MATCH_SIZE 20
#define RASTERFALL_PUBLIC_ERROR_SIZE 10
#define RASTERFALL_PUBLIC_REGISTERED_SIZE 9

static inline int rasterfall_public_room_valid(int room_id)
{
    return room_id >= RASTERFALL_PUBLIC_ROOM_MIN &&
           room_id <= RASTERFALL_PUBLIC_ROOM_MAX;
}

static inline int rasterfall_public_room_uses_relay(int room_id)
{
    return room_id >= RASTERFALL_PUBLIC_RELAY_ROOM_MIN &&
           room_id <= RASTERFALL_PUBLIC_ROOM_MAX;
}

#endif
