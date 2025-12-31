#include <raylib.h>
#include <algorithm>
#include <vector>
#include <memory>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <iostream>
#include <string>

// --- DIMENSIONS ---
constexpr int SCREEN_WIDTH = 1600;
constexpr int SCREEN_HEIGHT = 700;
constexpr int WORLD_WIDTH = 4000; 

constexpr int ROAD_HEIGHT = 140;
constexpr int LANE_HEIGHT = 45;
constexpr float VEHICLE_WIDTH = 90.0f;
constexpr float VEHICLE_HEIGHT = 40.0f;
constexpr float SAFE_DISTANCE = 45.0f;
constexpr int ROAD_Y_TOP = 110;
constexpr int ROAD_Y_BOTTOM = 280;

// --- FIX: ROBUST STAR DRAWING ---
// Draws triangles in both winding orders to guarantee visibility
void DrawStar(int cx, int cy, float outerRadius, float innerRadius, Color color) {
    Vector2 points[10];
    Vector2 center = { (float)cx, (float)cy };
    
    // Calculate the 10 points of the star
    for (int i = 0; i < 10; i++) {
        float angle = -PI / 2.0f + i * (PI / 5.0f); // Start at top (-90 deg)
        float r = (i % 2 == 0) ? outerRadius : innerRadius;
        points[i].x = cx + cosf(angle) * r;
        points[i].y = cy + sinf(angle) * r;
    }
    
    // Draw triangles connecting center to points
    for (int i = 0; i < 10; i++) {
        Vector2 p1 = points[i];
        Vector2 p2 = points[(i + 1) % 10];
        
        // Draw both windings to ensure it's not culled (invisible)
        DrawTriangle(p1, p2, center, color); 
        DrawTriangle(p2, p1, center, color); 
    }
}

enum MissionType {
    MISSION_NONE,
    MISSION_CALL_AMBULANCE,
    MISSION_CALL_TOW,
    MISSION_CALL_BUS
};

enum AmbulanceState {
    PATROL, TO_ACCIDENT, WAIT_AT_ACCIDENT, TO_HOSPITAL, WAIT_AT_HOSPITAL, LEAVING
};

enum BusState {
    BUS_TO_SCHOOL, BUS_WAIT_AT_SCHOOL, BUS_LEAVING
};

class TrafficLight {
private:
    Rectangle box;
    float timer;
    bool red;
    float cycleTime;
public:
    TrafficLight(float x, float y, float cycle = 5.0f)
        : box({ x, y, 20, 60 }), timer(0.0f), red(true), cycleTime(cycle) {}
    void Update(float delta) {
        timer += delta;
        if (timer >= cycleTime) {
            timer = 0.0f;
            red = !red;
        }
    }
    void Draw() const {
        DrawRectangleRec(box, DARKGRAY);
        DrawCircle((int)(box.x + 10), (int)(box.y + 15), 8.0f, red ? RED : Fade(RED, 0.3f));
        DrawCircle((int)(box.x + 10), (int)(box.y + 45), 8.0f, !red ? GREEN : Fade(GREEN, 0.3f));
    }
    bool IsRed() const { return red; }
    float GetStopLineX(bool rightToLeft) const {
        return rightToLeft ? (box.x - 40) : (box.x + box.width + 40);
    }
};

class Vehicle {
protected:
    float x, y, targetY, speed;
    Color color;
    bool moving, ambulance, depannage, schoolBus, dirRight, changedLane, forcedStop;
    Texture2D texture{};
public:
    bool isCrashed, toBeRemoved;
    bool isReckless, isAccidentTarget, isTowed, laneLock;
    float towOffsetX;
    Vehicle* myTower; 

    Vehicle(float startX, float startY, float spd, Color col, bool dir = true, bool amb = false, bool dep = false, bool bus = false)
        : x(startX), y(startY), targetY(startY), speed(spd), color(col), moving(true), ambulance(amb), depannage(dep), schoolBus(bus), dirRight(dir), changedLane(false), forcedStop(false), isCrashed(false), toBeRemoved(false), isReckless(false), isAccidentTarget(false), isTowed(false), laneLock(false), towOffsetX(0.0f), myTower(nullptr) {}
    virtual ~Vehicle() { UnloadTexture(texture); }
    
    virtual void Update(bool stopForRed = false) {
        if (isCrashed && !isTowed) return;
        if (isTowed) return;
        if (isReckless) { stopForRed = false; forcedStop = false; }
        if (moving && !stopForRed && !forcedStop) x += dirRight ? speed : -speed;
        if (fabs(targetY - y) > 0.5f) y += (targetY - y) * 0.08f; else y = targetY;
    }

    virtual void Draw() const {
        Rectangle source = { 0, 0, (float)texture.width, (float)texture.height };
        Rectangle dest = { x + VEHICLE_WIDTH / 2, y + VEHICLE_HEIGHT / 2, VEHICLE_HEIGHT, VEHICLE_WIDTH };
        Vector2 origin = { VEHICLE_HEIGHT / 2, VEHICLE_WIDTH / 2 };
        float rotation = dirRight ? 90.0f : -90.0f;
        DrawTexturePro(texture, source, dest, origin, rotation, isCrashed ? RED : WHITE);
    }
    
    bool IsOffScreen() const { return dirRight ? x > WORLD_WIDTH + 1500 : x < -1500; }
    bool IsAmbulance() const { return ambulance; }
    bool IsDepannage() const { return depannage; }
    bool IsSchoolBus() const { return schoolBus; }
    float GetX() const { return x; }
    float GetY() const { return y; }
    void SetX(float newX) { x = newX; }
    void SetY(float newY) { y = newY; }
    void SetSpeed(float s) { speed = s; }
    float GetSpeed() const { return speed; }
    void SetMoving(bool state) { moving = state; }
    bool IsMoving() const { return moving; }
    void SetTargetY(float newY) { targetY = newY; }
    float GetTargetY() const { return targetY; }
    bool HasChangedLane() const { return changedLane; }
    void SetChangedLane(bool v) { changedLane = v; }
    void SetForcedStop(bool stop) { forcedStop = stop; }
    bool IsForcedStop() const { return forcedStop; }
};

class Car : public Vehicle {
public:
    Car(float startX, float startY, float spd, Color col, bool dirRight = true, const char* imageFile = "car.png")
        : Vehicle(startX, startY, spd, col, dirRight) { texture = LoadTexture(imageFile); }
};

class SchoolBus : public Vehicle {
public:
    BusState state;
    float stateTimer;
    float schoolXLocation;
    SchoolBus(float startX, float startY, float spd)
        : Vehicle(startX, startY, spd, YELLOW, false, false, false, true), state(BUS_TO_SCHOOL), stateTimer(0.0f), schoolXLocation(WORLD_WIDTH / 2 + 100.0f) { texture = LoadTexture("school_bus.png"); }
    void Update(bool stopForRed = false) override {
        if (state == BUS_WAIT_AT_SCHOOL) {
            stateTimer += GetFrameTime();
            if (stateTimer >= 4.0f) state = BUS_LEAVING;
        } else if (!stopForRed && !forcedStop) {
            if (state == BUS_TO_SCHOOL) {
                x -= speed;
                if (x <= schoolXLocation) { x = schoolXLocation; state = BUS_WAIT_AT_SCHOOL; stateTimer = 0.0f; }
            } else if (state == BUS_LEAVING) x -= speed;
        }
        if (fabs(targetY - y) > 0.5f) y += (targetY - y) * 0.08f; else y = targetY;
    }
};

class Ambulance : public Vehicle {
public:
    AmbulanceState state;
    float stateTimer;
    float accidentX, accidentY;
    Ambulance(float startX, float startY, float spd, bool dirRight = false)
        : Vehicle(startX, startY, spd, RAYWHITE, dirRight, true), state(PATROL), stateTimer(0.0f), accidentX(0), accidentY(0) { texture = LoadTexture("ambulance.png"); }
    void AssignAccident(float accX, float accY) { accidentX = accX; accidentY = accY; state = TO_ACCIDENT; }
    void Update(bool stopForRed = false) override {
        switch (state) {
            case PATROL: Vehicle::Update(stopForRed); break;
            case TO_ACCIDENT: 
                if (dirRight) x += speed; else x -= speed; 
                if (x <= accidentX + 160.0f) { x = accidentX + 160.0f; state = WAIT_AT_ACCIDENT; moving = false; stateTimer = 0.0f; } break;
            case WAIT_AT_ACCIDENT: 
                moving = false;
                stateTimer += GetFrameTime(); if (stateTimer >= 5.0f) { state = TO_HOSPITAL; moving = true; } break;
            case TO_HOSPITAL: 
                if (!forcedStop) {
                    if (x > 80) x -= speed; 
                    else { state = WAIT_AT_HOSPITAL; moving = false; stateTimer = 0.0f; } 
                }
                break;
            case WAIT_AT_HOSPITAL: 
                moving = false;
                stateTimer += GetFrameTime(); if (stateTimer >= 5.0f) { state = LEAVING; moving = true; } break;
            case LEAVING: x -= speed; break;
        }
        if (fabs(targetY - y) > 0.5f) y += (targetY - y) * 0.08f; else y = targetY;
    }
};

class Depannage : public Vehicle {
public:
    bool hasPickedUp, isWorking;
    float targetX, workTimer;
    Depannage(float startX, float startY, float spd)
        : Vehicle(startX, startY, spd, ORANGE, false, false, true), hasPickedUp(false), targetX(0), workTimer(0.0f), isWorking(false) { texture = LoadTexture("depannage.png"); }
    void SetTarget(float tX) { targetX = tX; }
    void Update(bool stopForRed = false) override {
        if (isWorking) {
            moving = false;
            workTimer += GetFrameTime();
            if (workTimer > 2.0f) { hasPickedUp = true; isWorking = false; moving = true; }
            return;
        }
        if (!hasPickedUp && x <= targetX - 120) { isWorking = true; moving = false; workTimer = 0.0f; return; }
        Vehicle::Update(stopForRed);
    }
};

class Road {
public:
    void Draw() const {
        DrawRectangle(-5000, -5000, WORLD_WIDTH + 10000, ROAD_Y_TOP + 5000, DARKGREEN); 
        DrawRectangle(-5000, ROAD_Y_BOTTOM + ROAD_HEIGHT + 20, WORLD_WIDTH + 10000, 5000, DARKGREEN); 

        int gapY = ROAD_Y_TOP + ROAD_HEIGHT;
        int gapHeight = ROAD_Y_BOTTOM - gapY;
        DrawRectangle(-5000, gapY, WORLD_WIDTH + 10000, gapHeight, { 194, 178, 128, 255 }); 

        DrawRectangle(-5000, ROAD_Y_TOP, WORLD_WIDTH + 10000, ROAD_HEIGHT, { 40, 40, 40, 255 });
        DrawRectangle(-5000, ROAD_Y_BOTTOM, WORLD_WIDTH + 10000, ROAD_HEIGHT, { 40, 40, 40, 255 });

        for (int i = 1; i < 3; i++) {
            DrawLine(-5000, ROAD_Y_TOP + i * LANE_HEIGHT, WORLD_WIDTH + 5000, ROAD_Y_TOP + i * LANE_HEIGHT, Fade(WHITE, 0.7f));
            DrawLine(-5000, ROAD_Y_BOTTOM + i * LANE_HEIGHT, WORLD_WIDTH + 5000, ROAD_Y_BOTTOM + i * LANE_HEIGHT, Fade(WHITE, 0.7f));
        }
        DrawRectangle(-5000, ROAD_Y_TOP - 20, WORLD_WIDTH + 10000, 20, GRAY);
        DrawRectangle(-5000, ROAD_Y_BOTTOM + ROAD_HEIGHT, WORLD_WIDTH + 10000, 20, GRAY);
        for (int i = -5000; i < WORLD_WIDTH + 5000; i += 80) {
            DrawRectangle(i, ROAD_Y_TOP + (ROAD_HEIGHT / 2) - 3, 40, 6, YELLOW);
            DrawRectangle(i, ROAD_Y_BOTTOM + (ROAD_HEIGHT / 2) - 3, 40, 6, YELLOW);
        }
    }
};

struct Accident { bool active, pending; float x, y; Vehicle* car1; Vehicle* car2; };

class Simulation {
private:
    Camera2D camera = { 0 }; 
    std::vector<std::unique_ptr<Vehicle>> vehiclesTop;
    std::vector<std::unique_ptr<Vehicle>> vehiclesBottom;
    TrafficLight lightTop, lightBottom;
    Road road;
    
    Texture2D hospitalTexture{}, schoolTexture{}, houseTextures[3]{}, jungleTexture{}, seaTexture{};

    float laneYTop[3], laneYBottom[3];
    float carSpawnTimerTop, carSpawnTimerBottom;
    const char* carImages[5] = { "car.png", "cars.png", "car2.png", "car3.png", "car4.png" };
    Sound siren{};
    bool ambulanceActive = false, screenAlertOn = false, waitingForTowToLeave = false;
    float screenAlertTimer = 0.0f;
    Accident currentAccident;

    int playerStars = 3;
    MissionType currentMission = MISSION_NONE;
    float missionTimer = 0.0f;
    float missionMaxTime = 8.0f;
    float busCooldown = 15.0f;
    bool gameOver = false;

public:
    Simulation() : lightTop(WORLD_WIDTH / 2 - 80, ROAD_Y_TOP - 80, 5.0f), lightBottom(WORLD_WIDTH / 2 - 150, ROAD_Y_BOTTOM + ROAD_HEIGHT + 20, 5.0f), carSpawnTimerTop(0.0f), carSpawnTimerBottom(0.0f) {
        for (int i = 0; i < 3; i++) { laneYTop[i] = (float)ROAD_Y_TOP + 10.0f + i * (float)LANE_HEIGHT; laneYBottom[i] = (float)ROAD_Y_BOTTOM + 10.0f + i * (float)LANE_HEIGHT; }
        currentAccident = { false, false, 0, 0, nullptr, nullptr };
    }

    void Init() {
        siren = LoadSound("siren.wav");
        srand((unsigned int)time(nullptr));
        
        hospitalTexture = LoadTexture("hospital.png");
        schoolTexture = LoadTexture("school.png");
        houseTextures[0] = LoadTexture("house.png");
        houseTextures[1] = LoadTexture("house1.jpg");
        houseTextures[2] = LoadTexture("house2.png");
        jungleTexture = LoadTexture("jungle.png");
        seaTexture = LoadTexture("sea.png");

        camera.target = { WORLD_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f };
        camera.offset = { SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f };
        camera.rotation = 0.0f;
        camera.zoom = 1.0f;
    }

    // --- RESTART GAME LOGIC ---
    void Reset() {
        vehiclesTop.clear();
        vehiclesBottom.clear();
        currentAccident = { false, false, 0, 0, nullptr, nullptr };
        currentMission = MISSION_NONE;
        playerStars = 3;
        gameOver = false;
        ambulanceActive = false;
        waitingForTowToLeave = false;
        screenAlertOn = false;
        missionTimer = 0.0f;
        busCooldown = 15.0f;
        
        camera.target = { WORLD_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f };
        camera.zoom = 1.0f;
    }

    void SpawnCarTop() {
        int lane = GetRandomValue(0, 2);
        float speed = 2.0f + GetRandomValue(0, 5) / 10.0f;
        Color c = { (unsigned char)GetRandomValue(80, 255), (unsigned char)GetRandomValue(80, 255), (unsigned char)GetRandomValue(80, 255), 255 };
        vehiclesTop.push_back(std::make_unique<Car>(-1500, laneYTop[lane], speed, c, true, carImages[GetRandomValue(0, 4)]));
    }
    void SpawnCarBottom() {
        int lane = GetRandomValue(0, 2);
        float speed = 2.0f + GetRandomValue(0, 5) / 10.0f; 
        Color c = { (unsigned char)GetRandomValue(80, 255), (unsigned char)GetRandomValue(80, 255), (unsigned char)GetRandomValue(80, 255), 255 };
        vehiclesBottom.push_back(std::make_unique<Car>(WORLD_WIDTH + 1500, laneYBottom[lane], speed, c, false, carImages[GetRandomValue(0, 4)]));
    }
    
    void CallSchoolBus() { 
        if (currentMission == MISSION_CALL_BUS) { currentMission = MISSION_NONE; }
        vehiclesBottom.push_back(std::make_unique<SchoolBus>(WORLD_WIDTH + 1500, laneYBottom[2], 2.5f)); 
    }

    void TriggerRandomAccident() {
        if (waitingForTowToLeave) return; 
        if (currentAccident.active || currentAccident.pending) return;
        for (size_t i = 0; i < vehiclesBottom.size(); i++) {
            Vehicle* v1 = vehiclesBottom[i].get(); 
            if (v1->IsAmbulance() || v1->IsDepannage() || v1->IsSchoolBus() || v1->IsOffScreen()) continue;
            if (v1->isTowed || v1->isCrashed) continue;
            if (fabs(v1->GetTargetY() - laneYBottom[2]) < 5.0f) continue;
            for (size_t j = 0; j < vehiclesBottom.size(); j++) {
                if (i == j) continue;
                Vehicle* v2 = vehiclesBottom[j].get(); 
                if (v2->IsAmbulance() || v2->IsDepannage() || v2->IsSchoolBus() || v2->IsOffScreen()) continue;
                if (v2->isTowed || v2->isCrashed) continue;
                if (fabs(v1->GetTargetY() - v2->GetTargetY()) < 5.0f) {
                    if (v1->GetX() > v2->GetX()) {
                        float dist = v1->GetX() - v2->GetX();
                        if (dist < 400 && dist > 110 && v1->GetX() < WORLD_WIDTH - 100 && v2->GetX() > 100) {
                            currentAccident.pending = true; currentAccident.car1 = v2; currentAccident.car2 = v1; waitingForTowToLeave = true;
                            v1->isReckless = true; v2->isAccidentTarget = true; v1->laneLock = true; v2->laneLock = true; 
                            v1->SetSpeed(v1->GetSpeed() * 2.8f); v2->SetSpeed(v2->GetSpeed() * 0.4f);
                            currentMission = MISSION_CALL_AMBULANCE; missionTimer = missionMaxTime;
                            return;
                        }
                    }
                }
            }
        }
    }
    
    void CallAmbulance() {
        if (!currentAccident.active && !currentAccident.pending) TriggerRandomAccident(); 
        if (currentMission == MISSION_CALL_AMBULANCE) { currentMission = MISSION_NONE; }
        PlaySound(siren);
        auto amb = std::make_unique<Ambulance>(WORLD_WIDTH + 1500, laneYBottom[1], 4.5f, false);
        if (currentAccident.active) { amb->AssignAccident(currentAccident.x, currentAccident.y); amb->SetTargetY(currentAccident.y); }
        vehiclesBottom.push_back(std::move(amb)); ambulanceActive = true;
    }

    void CallDepannage() {
        if (!currentAccident.active) return;
        if (currentMission == MISSION_CALL_TOW) { currentMission = MISSION_NONE; }
        auto tow = std::make_unique<Depannage>(WORLD_WIDTH + 1500, currentAccident.y, 2.5f);
        tow->SetTarget(currentAccident.x); vehiclesBottom.push_back(std::move(tow));
    }

    void Update(float delta) {
        if (gameOver) return;

        float wheel = GetMouseWheelMove();
        if (wheel != 0) { camera.zoom += wheel * 0.1f; if (camera.zoom < 0.5f) camera.zoom = 0.5f; if (camera.zoom > 2.0f) camera.zoom = 2.0f; }
        if (IsKeyDown(KEY_RIGHT) || (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && GetMouseDelta().x < 0)) camera.target.x += 15.0f / camera.zoom;
        if (IsKeyDown(KEY_LEFT) || (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && GetMouseDelta().x > 0)) camera.target.x -= 15.0f / camera.zoom;
        if (camera.target.x < 0) camera.target.x = 0; if (camera.target.x > WORLD_WIDTH) camera.target.x = WORLD_WIDTH;

        carSpawnTimerTop += delta; if (carSpawnTimerTop >= GetRandomValue(40, 70) / 10.0f) { carSpawnTimerTop = 0.0f; SpawnCarTop(); } 
        carSpawnTimerBottom += delta; if (carSpawnTimerBottom >= GetRandomValue(40, 70) / 10.0f) { carSpawnTimerBottom = 0.0f; SpawnCarBottom(); }
        
        if (GetRandomValue(0, 1000) < 5) TriggerRandomAccident();
        
        busCooldown -= delta;
        if (busCooldown <= 0 && currentMission == MISSION_NONE) {
            if (GetRandomValue(0, 100) < 2) { 
                currentMission = MISSION_CALL_BUS;
                missionTimer = missionMaxTime;
                busCooldown = 15.0f;
            }
        }

        if (currentMission != MISSION_NONE) {
            missionTimer -= delta;
            if (missionTimer <= 0.0f) {
                playerStars--;
                currentMission = MISSION_NONE;
                if (playerStars <= 0) gameOver = true;
            }
        }

        lightTop.Update(delta); lightBottom.Update(delta);

        vehiclesTop.erase(std::remove_if(vehiclesTop.begin(), vehiclesTop.end(), [](const std::unique_ptr<Vehicle>& v) { return v->IsOffScreen(); }), vehiclesTop.end());
        vehiclesBottom.erase(std::remove_if(vehiclesBottom.begin(), vehiclesBottom.end(), [&](const std::unique_ptr<Vehicle>& v) { 
            if (v->IsDepannage()) { bool despawn = v->GetX() < -3000.0f; if (despawn) waitingForTowToLeave = false; return despawn; }
            if (v->IsSchoolBus()) return v->IsOffScreen();
            if (v->isReckless || v->isAccidentTarget || v->isCrashed || v->isTowed) {
                if (v->GetX() > -2000.0f && !v->toBeRemoved) return false;
                if (v.get() == currentAccident.car1) currentAccident.car1 = nullptr; if (v.get() == currentAccident.car2) currentAccident.car2 = nullptr;
                if (v->isAccidentTarget || v->isReckless || v->isCrashed) { currentAccident.pending = false; currentAccident.active = false; } return true;
            }
            if (v->IsOffScreen() || v->toBeRemoved) {
                if (v.get() == currentAccident.car1 || v.get() == currentAccident.car2) { currentAccident.car1 = nullptr; currentAccident.car2 = nullptr; currentAccident.pending = false; currentAccident.active = false; waitingForTowToLeave = false; } return true;
            } return false;
        }), vehiclesBottom.end());

        if (currentAccident.pending && currentAccident.car1 && currentAccident.car2) {
            float dist = currentAccident.car2->GetX() - currentAccident.car1->GetX();
            if (dist < VEHICLE_WIDTH - 10.0f && dist > -VEHICLE_WIDTH) {
                currentAccident.pending = false; currentAccident.active = true; currentAccident.car1->isCrashed = true; currentAccident.car2->isCrashed = true;
                currentAccident.car2->isReckless = false; currentAccident.car1->SetMoving(false); currentAccident.car2->SetMoving(false);
                currentAccident.x = currentAccident.car1->GetX() + (VEHICLE_WIDTH/2); currentAccident.y = currentAccident.car1->GetY();
                for (auto& v : vehiclesBottom) { if (v->IsAmbulance()) { static_cast<Ambulance*>(v.get())->AssignAccident(currentAccident.x, currentAccident.y); v->SetTargetY(currentAccident.y); } }
            }
        } else if (currentAccident.pending) { currentAccident.pending = false; waitingForTowToLeave = false; }

        Ambulance* activeAmbulance = nullptr; Depannage* activeTow = nullptr;
        for (auto& v : vehiclesBottom) { if (v->IsAmbulance()) activeAmbulance = static_cast<Ambulance*>(v.get()); if (v->IsDepannage()) activeTow = static_cast<Depannage*>(v.get()); }

        if (activeTow && activeTow->hasPickedUp && currentAccident.active) {
            if (currentAccident.car1) { currentAccident.car1->isTowed = true; currentAccident.car1->isCrashed = false; currentAccident.car1->isAccidentTarget = false; currentAccident.car1->towOffsetX = 100.0f; currentAccident.car1->myTower = activeTow; currentAccident.car1->SetY(activeTow->GetY()); }
            if (currentAccident.car2) { currentAccident.car2->isTowed = true; currentAccident.car2->isCrashed = false; currentAccident.car2->towOffsetX = 200.0f; currentAccident.car2->myTower = activeTow; currentAccident.car2->SetY(activeTow->GetY()); }
            currentAccident.active = false; 
        }
        for (auto& v : vehiclesBottom) { if (v->isTowed && v->myTower != nullptr) { v->SetX(v->myTower->GetX() + v->towOffsetX); v->SetY(v->myTower->GetY()); } }

        if (activeAmbulance) { 
            if (activeAmbulance->state == TO_HOSPITAL) {
                if (currentAccident.active && !activeTow && currentMission == MISSION_NONE) {
                    currentMission = MISSION_CALL_TOW;
                    missionTimer = missionMaxTime;
                }
                activeAmbulance->SetTargetY(laneYBottom[2]); 
            }
            else if (activeAmbulance->state == TO_ACCIDENT && currentAccident.active) activeAmbulance->SetTargetY(currentAccident.y); 
        }

        for (size_t i = 0; i < vehiclesBottom.size(); ++i) {
            auto& v = vehiclesBottom[i]; if (v->isCrashed || v->isTowed) continue; 
            
            if (!v->isReckless && !v->laneLock && !v->HasChangedLane()) {
                auto tryYield = [&](Vehicle* emergencyVehicle) {
                    if (emergencyVehicle) { 
                         if (fabs(v->GetTargetY() - emergencyVehicle->GetTargetY()) < 5.0f) {
                             float dist = emergencyVehicle->GetX() - v->GetX();
                             if (dist > 0 && dist < 450.0f) { 
                                 int currentLaneIdx = 0; if (fabs(v->GetY() - laneYBottom[1]) < 5) currentLaneIdx = 1; if (fabs(v->GetY() - laneYBottom[2]) < 5) currentLaneIdx = 2;
                                 int targetLane = (currentLaneIdx + 1) % 3; v->SetTargetY(laneYBottom[targetLane]); v->SetChangedLane(true);
                             }
                          }
                    }
                };
                tryYield(activeAmbulance); if (activeTow && activeTow->isWorking) tryYield(activeTow); 
            }

            bool stop = false;
            if (!v->isReckless) {
                if (currentAccident.active && !v->HasChangedLane() && !v->laneLock) {
                    if (fabs(v->GetY() - currentAccident.y) < 5.0f && v->GetX() > currentAccident.x) {
                        if (v->GetX() - currentAccident.x < 300) {
                            int currentLaneIdx = 0; if (fabs(v->GetY() - laneYBottom[1]) < 5) currentLaneIdx = 1; if (fabs(v->GetY() - laneYBottom[2]) < 5) currentLaneIdx = 2;
                            int targetLane = (currentLaneIdx + 1) % 3; v->SetTargetY(laneYBottom[targetLane]); v->SetChangedLane(true);
                        }
                    }
                }
                float stopX = lightBottom.GetStopLineX(true);
                if (!v->IsAmbulance() && lightBottom.IsRed() && fabs(v->GetX() - stopX) < 50) stop = true;
                
                if (!stop) {
                    for (size_t j = 0; j < vehiclesBottom.size(); ++j) {
                        if (i == j) continue; auto& other = vehiclesBottom[j]; if (other->isTowed) continue; 
                        if (v->IsDepannage() && (other->isCrashed || other->isAccidentTarget)) continue;
                        
                        if (fabs(v->GetTargetY() - other->GetTargetY()) < 5.0f) {
                            if (other->GetX() < v->GetX()) { 
                                float frontOfOther = other->GetX() + VEHICLE_WIDTH; 
                                float distToFront = v->GetX() - frontOfOther;
                                float limit = SAFE_DISTANCE;
                                
                                if (v->IsAmbulance()) {
                                    limit = 10.0f;
                                } else if (other->IsDepannage()) {
                                    limit = 250.0f; 
                                } else if (other->IsAmbulance() && !other->IsMoving()) {
                                     limit = 150.0f; 
                                }

                                if (distToFront < limit) { stop = true; break; }
                            }
                        }
                    }
                }
            } 
            v->SetForcedStop(stop); v->Update(stop);
        }

        for (size_t i = 0; i < vehiclesTop.size(); ++i) {
             auto& v = vehiclesTop[i]; bool stop = false;
             if (lightTop.IsRed() && fabs(v->GetX() - lightTop.GetStopLineX(false)) < 50) stop = true;
             if (!stop) {
                 for (size_t j = 0; j < vehiclesTop.size(); ++j) {
                     if (i == j) continue; if (vehiclesTop[j]->GetTargetY() == v->GetTargetY() && vehiclesTop[j]->GetX() > v->GetX()) { if (vehiclesTop[j]->GetX() - VEHICLE_WIDTH - v->GetX() < SAFE_DISTANCE) { stop = true; break; } }
                 }
             }
             v->SetForcedStop(stop); v->Update(stop);
        }

        ambulanceActive = (activeAmbulance != nullptr);
        if (ambulanceActive) { screenAlertTimer += delta; if (screenAlertTimer >= 0.5f) { screenAlertOn = !screenAlertOn; screenAlertTimer = 0.0f; } } else { screenAlertOn = false; }
    }

    void DrawWorld() const {
        road.Draw();
        
        if (jungleTexture.id != 0) {
            int jWidth = jungleTexture.width; if (jWidth == 0) jWidth = 100;
            float jHeight = (float)jungleTexture.height;
            for (int i = -2000; i < WORLD_WIDTH + 2000; i += jWidth) {
                Rectangle source = { 0.0f, 0.0f, (float)jWidth, jHeight };
                Rectangle destTop = { (float)i, -450.0f, (float)jWidth, 350.0f };
                DrawTexturePro(jungleTexture, source, destTop, {0,0}, 0.0f, WHITE);
            }
        }

        if (seaTexture.id != 0) {
            int sWidth = seaTexture.width; if (sWidth == 0) sWidth = 100;
            float sHeight = (float)seaTexture.height;
            float time = (float)GetTime();
            DrawRectangle(-2000, 650, WORLD_WIDTH + 4000, 500, { 237, 201, 175, 255 }); 
            for (int i = -2000; i < WORLD_WIDTH + 2000; i += sWidth) {
                bool flip = ((i / sWidth) % 2 != 0); 
                float widthFactor = flip ? -1.0f : 1.0f;
                float waveY = sinf(time * 2.0f + (i * 0.005f)) * 5.0f;
                Rectangle source = { 0.0f, 0.0f, (float)sWidth * widthFactor, sHeight };
                Rectangle destBot = { (float)i, 700.0f + waveY, (float)sWidth, 350.0f };
                DrawTexturePro(seaTexture, source, destBot, {0,0}, 0.0f, WHITE);
            }
        }

        lightTop.Draw();
        lightBottom.Draw();
        
        DrawTexture(hospitalTexture, 10, ROAD_Y_BOTTOM + ROAD_HEIGHT + 10, WHITE);

        float time = (float)GetTime();
        float bounce = sinf(time * 6.0f) * 8.0f; 

        // --- ARROW 1: ACCIDENT ---
        if (currentAccident.active) {
            float accX = currentAccident.x;
            float accY = currentAccident.y - 100.0f + bounce;
            Color accCol = Fade(RED, 0.9f);
            
            // Draw big red arrow pointing down at the crash
            DrawRectangle((int)accX - 10, (int)accY, 20, 40, accCol);
            DrawTriangle({ accX, accY + 70 }, { accX + 25, accY + 40 }, { accX - 25, accY + 40 }, accCol);
            DrawText("ACCIDENT!", (int)accX - 50, (int)accY - 30, 20, RED);
        }

        // --- ARROW 2: HOSPITAL ---
        float hospCenterX = 75.0f; 
        float hospBaseY = 350.0f + bounce;
        Color arrowCol = Fade(RED, 0.8f); 
        DrawRectangle(hospCenterX - 10, hospBaseY, 20, 40, arrowCol);
        DrawTriangle({ hospCenterX, hospBaseY + 70 }, { hospCenterX + 25, hospBaseY + 40 }, { hospCenterX - 25, hospBaseY + 40 }, arrowCol);
        DrawText("HOSPITAL", hospCenterX - 40, hospBaseY - 30, 20, RED);

        // --- ARROW 3: SCHOOL ---
        float schoolCenterX = WORLD_WIDTH / 2 - 65.0f;
        float schoolBaseY = 350.0f + bounce; 
        Color schoolArrowCol = Fade(ORANGE, 0.8f);
        DrawRectangle(schoolCenterX - 10, schoolBaseY, 20, 40, schoolArrowCol);
        DrawTriangle({ schoolCenterX, schoolBaseY + 70 }, { schoolCenterX + 25, schoolBaseY + 40 }, { schoolCenterX - 25, schoolBaseY + 40 }, schoolArrowCol);
        DrawText("SCHOOL", schoolCenterX - 35, schoolBaseY - 30, 20, ORANGE);
        
        // HOUSES
        DrawTexture(houseTextures[1], -1500, 440, WHITE);
        DrawTexture(houseTextures[2], -1250, 423, WHITE);
        DrawTexture(houseTextures[1], -1020, 440, WHITE);
        DrawTexture(houseTextures[0], -850, 410, WHITE);
        DrawTexture(houseTextures[0], -600, 410, WHITE);
        DrawTexture(houseTextures[1], -250, 440, WHITE);
        DrawTexture(houseTextures[0], 250, 410, WHITE);
        DrawTexture(houseTextures[1], 600, 440, WHITE);
        DrawTexture(houseTextures[2], 850, 423, WHITE);
        DrawTexture(houseTextures[1], 1100, 440, WHITE);
        DrawTexture(houseTextures[0], 1400, 410, WHITE);
        DrawTexture(houseTextures[1], 2400, 440, WHITE);
        DrawTexture(houseTextures[0], 2600, 410, WHITE);
        DrawTexture(houseTextures[1], 2900, 440, WHITE);
        DrawTexture(houseTextures[0], 3200, 410, WHITE);
        DrawTexture(houseTextures[2], 3550, 423, WHITE);
        DrawTexture(houseTextures[2], 3850, 423, WHITE);
        DrawTexture(houseTextures[2], 4100, 423, WHITE);
        DrawTexture(houseTextures[0], 4300, 410, WHITE);
        DrawTexture(houseTextures[1], 4700, 440, WHITE);
        DrawTexture(houseTextures[2], 5000, 423, WHITE);
        DrawTexture(houseTextures[1], -1500, -125, WHITE);
        DrawTexture(houseTextures[2], -1250, -125, WHITE);
        DrawTexture(houseTextures[1], -1020, -125, WHITE);
        DrawTexture(houseTextures[0], -850, -145, WHITE);
        DrawTexture(houseTextures[0], -600, -145, WHITE);
        DrawTexture(houseTextures[1], -250, -125, WHITE);
        DrawTexture(houseTextures[1], 0, -125, WHITE);
        DrawTexture(houseTextures[0], 250,-145, WHITE);
        DrawTexture(houseTextures[1], 600, -118, WHITE);
        DrawTexture(houseTextures[2], 850, -125, WHITE);
        DrawTexture(houseTextures[1], 1100, -118, WHITE);
        DrawTexture(houseTextures[0], 1400, -145, WHITE);
        DrawTexture(houseTextures[1], 2050, -118, WHITE);
        DrawTexture(houseTextures[1], 2400, -118, WHITE);
        DrawTexture(houseTextures[0], 2600, -145, WHITE);
        DrawTexture(houseTextures[1], 2950, -118, WHITE);
        DrawTexture(houseTextures[0], 3200, -145, WHITE);
        DrawTexture(houseTextures[2], 3550, -125, WHITE);
        DrawTexture(houseTextures[2], 3850, -125, WHITE);
        DrawTexture(houseTextures[2], 4100, -125, WHITE);
        DrawTexture(houseTextures[0], 4300, -145, WHITE);
        DrawTexture(houseTextures[1], 4700, -118, WHITE);
        DrawTexture(houseTextures[2], 5000, -125, WHITE);

        DrawTexture(schoolTexture , WORLD_WIDTH / 2 - 130, 430, WHITE);
        
        for (auto& v : vehiclesTop) v->Draw();
        for (auto& v : vehiclesBottom) v->Draw();
    }

    void DrawUI() {
        if (gameOver) {
            DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.8f));
            DrawText("GAME OVER", SCREEN_WIDTH/2 - 200, SCREEN_HEIGHT/2 - 100, 80, RED);
            DrawText("Mission Failed! No Stars Left.", SCREEN_WIDTH/2 - 180, SCREEN_HEIGHT/2, 30, WHITE);
            
            // --- RESTART BUTTON ---
            Rectangle btn = { (float)SCREEN_WIDTH/2 - 100, (float)SCREEN_HEIGHT/2 + 80, 200, 60 };
            Vector2 mouse = GetMousePosition();
            bool hover = CheckCollisionPointRec(mouse, btn);
            
            DrawRectangleRec(btn, hover ? DARKGREEN : GREEN);
            DrawRectangleLinesEx(btn, 3, WHITE);
            DrawText("RESTART", (int)btn.x + 35, (int)btn.y + 15, 30, WHITE);
            
            if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                Reset(); // Call Reset logic
            }
            return;
        }

        if (screenAlertOn) {
            DrawRectangle(0, 0, 20, SCREEN_HEIGHT, Fade(RED, 0.7f));
            DrawRectangle(SCREEN_WIDTH - 20, 0, 20, SCREEN_HEIGHT, Fade(RED, 0.7f));
        }
        
        // --- DRAW STARS (Visible 5-Pointed) ---
        DrawText(TextFormat("LIVES:"), 20, 80, 30, GOLD);
        for(int i=0; i<playerStars; i++) {
            DrawStar(160 + (i * 45), 95, 15, 7, GOLD);
        }

        // --- DRAW MISSION STATUS ---
        if (currentMission != MISSION_NONE) {
            std::string msg = "";
            Color c = WHITE;
            if (currentMission == MISSION_CALL_AMBULANCE) { msg = "MISSION: CALL AMBULANCE (E)!"; c = RED; }
            else if (currentMission == MISSION_CALL_TOW) { msg = "MISSION: CALL TOW TRUCK (D)!"; c = ORANGE; }
            else if (currentMission == MISSION_CALL_BUS) { msg = "MISSION: SEND SCHOOL BUS (S)!"; c = YELLOW; }
            
            DrawRectangle(SCREEN_WIDTH/2 - 250, 10, 500, 60, Fade(BLACK, 0.7f));
            DrawText(msg.c_str(), SCREEN_WIDTH/2 - 200, 25, 25, c);
            
            float ratio = missionTimer / missionMaxTime;
            DrawRectangle(SCREEN_WIDTH/2 - 240, 55, (int)(480 * ratio), 10, c);
        }

        if(currentAccident.active) DrawText("ACCIDENT ACTIVE!", SCREEN_WIDTH/2 - 100, 80, 20, RED);
        
        DrawText("Use MOUSE WHEEL to Zoom", 20, 20, 20, WHITE);
        DrawText("Use ARROW KEYS to Pan", 20, 45, 20, WHITE);
    }

    void Draw() {
        BeginMode2D(camera);
            DrawWorld();
        EndMode2D();
        
        DrawUI();
    }

    bool DrawIntroScreen() {
        BeginMode2D(camera);
        road.Draw();
        EndMode2D();

        DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.85f));
        DrawText("TRAFFIC & EMERGENCY SIMULATION", SCREEN_WIDTH/2 - 300, 100, 40, GOLD);

        int boxX = SCREEN_WIDTH/2 - 350;
        int boxY = 200;
        DrawRectangle(boxX, boxY, 700, 320, Fade(DARKBLUE, 0.5f)); 
        DrawRectangleLines(boxX, boxY, 700, 320, LIGHTGRAY);
        
        DrawText("CONTROLS & RULES:", boxX + 20, boxY + 20, 30, WHITE);
        DrawText("- Complete MISSIONS to keep your STARS.", boxX + 40, boxY + 70, 20, GOLD);
        DrawText("- Press 'E' when Accident occurs (Red Mission).", boxX + 40, boxY + 110, 20, WHITE);
        DrawText("- Press 'D' after Ambulance leaves (Orange Mission).", boxX + 40, boxY + 150, 20, WHITE);
        DrawText("- Press 'S' for School Run (Yellow Mission).", boxX + 40, boxY + 190, 20, WHITE);
        DrawText("- If Timer runs out, you lose a STAR.", boxX + 40, boxY + 230, 20, RED);
        
        Rectangle btnBounds = { (float)SCREEN_WIDTH/2 - 100, (float)SCREEN_HEIGHT - 100, 200, 60 };
        Vector2 mousePoint = GetMousePosition();
        bool btnHover = CheckCollisionPointRec(mousePoint, btnBounds);
        
        DrawRectangleRec(btnBounds, btnHover ? GREEN : DARKGREEN);
        DrawRectangleLinesEx(btnBounds, 3, WHITE);
        DrawText("START GAME", (int)btnBounds.x + 25, (int)btnBounds.y + 15, 24, WHITE);

        if (btnHover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            return true; 
        }
        
        return false;
    }

    ~Simulation() {
        UnloadSound(siren);
        UnloadTexture(hospitalTexture);
        UnloadTexture(schoolTexture);
        for(int i=0; i<3; i++) UnloadTexture(houseTextures[i]);
        UnloadTexture(jungleTexture);
        UnloadTexture(seaTexture);
    }
};

int main() {
    InitAudioDevice();
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Traffic Sim: Final Fixes");
    SetTargetFPS(60);
    
    {
        Simulation sim;
        sim.Init();
        bool gameStarted = false; 

        while (!WindowShouldClose()) {
            float delta = GetFrameTime();

            if (gameStarted) {
                sim.Update(delta);
            }

            BeginDrawing();
            ClearBackground(SKYBLUE);

            if (!gameStarted) {
                if (sim.DrawIntroScreen()) {
                    gameStarted = true;
                }
            } else {
                sim.Draw();
            }

            EndDrawing();

            if (gameStarted) {
                if (IsKeyPressed(KEY_E)) sim.CallAmbulance();
                if (IsKeyPressed(KEY_D)) sim.CallDepannage();
                if (IsKeyPressed(KEY_A)) sim.TriggerRandomAccident();
                if (IsKeyPressed(KEY_S)) sim.CallSchoolBus(); 
            }
        }
    } 

    CloseAudioDevice();
    CloseWindow();
    return 0;
}
