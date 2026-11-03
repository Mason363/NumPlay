/* Moto X3M: the bike, from the game's levels/1.json and levels/bike.json (made by tools/pack.py) */
#ifndef MX_BIKE_DATA_H
#define MX_BIKE_DATA_H
#define CAR_axisBackAngle 1.57079633f
#define CAR_axisBackLen 14.0f
#define CAR_axisBackY 13.0f
#define CAR_axisFrontAngle 1.09955743f
#define CAR_axisFrontLen 14.0f
#define CAR_axisFrontY 13.0f
#define CAR_baseHealth 2700.0f
#define CAR_bodyDensity 1.0f
#define CAR_bodyH 24.0f
#define CAR_bodyMassType 1.0f
#define CAR_bodyW 96.0f
#define CAR_boostCount 3.0f
#define CAR_boostForce 0.0f
#define CAR_boostValue 0.0f
#define CAR_damperBackDamp 0.0f
#define CAR_damperBackFreq 5.0f
#define CAR_damperBackLen 12.0f
#define CAR_damperBackY 12.0f
#define CAR_damperFrontDamp 0.0f
#define CAR_damperFrontFreq 5.0f
#define CAR_damperFrontLen 12.0f
#define CAR_damperFrontY 12.0f
#define CAR_driverDensity 1.0f
#define CAR_driverDx 45.0f
#define CAR_driverDy -16.0f
#define CAR_driverH 32.0f
#define CAR_driverW 32.0f
#define CAR_engineCount 3.0f
#define CAR_flipTorque 200000.0f
#define CAR_handlingCount 3.0f
#define CAR_handlingPct 0.0f
#define CAR_health 0.0f
#define CAR_healthCount 3.0f
#define CAR_healthValue 900.0f
#define CAR_maxFlipSpeed 0.2f
#define CAR_maxSpeed 700.0f
#define CAR_maxSpeedPct 2.0f
#define CAR_maxTorque 600000.0f
#define CAR_skinId 1.0f
#define CAR_torque 400000.0f
#define CAR_torquePct 5.0f
#define CAR_wd 0.0f
#define CAR_weapon 0.0f
#define CAR_wheelBackDensity 1.0f
#define CAR_wheelBackDx 34.0f
#define CAR_wheelBackFriction 6.0f
#define CAR_wheelBackR 19.0f
#define CAR_wheelFrictionPct 0.0f
#define CAR_wheelFrontDensity 3.0f
#define CAR_wheelFrontDx 45.0f
#define CAR_wheelFrontFriction 5.0f
#define CAR_wheelFrontR 19.0f
#define CAR_wheelRadiusValue 0.0f
#define CAR_wheels 0.0f
#define CAR_wheelsCount 3.0f
typedef struct { float x, y, w, h, r; } PartGeo;
static const PartGeo GEO_DRIVER_HEAD = {-1.8f, -59.5f, 25.2f, 23.1f, 0.26179486f};
static const PartGeo GEO_DRIVER_CORPSE = {-8.35f, -33.75f, 14.1f, 30.2f, 0.274591616f};
static const PartGeo GEO_DRIVER_ARM = {-4.25f, -39.4f, 16.1f, 10.1f, 0.78539044f};
static const PartGeo GEO_DRIVER_FOREARM = {8.8f, -33.35f, 19.1f, 6.0f, 0.192002197f};
static const PartGeo GEO_DRIVER_HIP = {-7.4f, -18.5f, 13.1f, 9.1f, 0.523596645f};
static const PartGeo GEO_DRIVER_SHIN = {-1.8f, -2.7f, 9.1f, 31.2f, 0.0523811828f};
static const V2 GEO_HIP_JOINT = {-3.0f, -15.0f};
static const V2 GEO_CORPSE_JOINT = {-11.0f, -23.2f};
static const V2 GEO_FOREARM_JOINT = {0.85f, -35.15f};
static const V2 GEO_ARM_JOINT = {-7.75f, -43.65f};
static const V2 GEO_HEAD_JOINT = {-5.6f, -48.5f};
static const V2 GEO_STEER_JOINT = {16.05f, -30.5f};
static const V2 GEO_SHIN_JOINT = {-1.0f, 10.0f};
static const V2 GEO_SUSPEND_BACK = {-6.0f, 12.0f};
#define GEO_SUSPEND_BACK_W 28.1f
static const V2 GEO_POINTS[4] = {{-12, 6}, {-40, -17}, {23, -28}, {15, 8}};
#endif
