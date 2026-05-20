-- MarioControllerAnimGraph.lua
-- Mario2 캐릭터용 엔진 AnimGraph / StateMachine 컨트롤러
--
-- 상위 StateMachine 구조:
--   LOCOMOTION (내부 sub-state: WAIT / WALK / RUN)
--   JUMP1
--   JUMP2
--   JUMP3
--
-- 입력:
--   WASD / 방향키 : 이동
--   SHIFT         : 달리기
--   SPACE         : 3단 점프 체인

local Vec = FVector.new
local Rot = FRotator.new

-- ─────────────────────────────────────────────────────────────
-- 에셋 경로
-- ─────────────────────────────────────────────────────────────
local MESH_PATH = "Asset/FBX/Mario2/Mario2.asset"

local ANIM_PATH = {
    WAIT  = "Asset/FBX/Mario2/animation/RoswellWait.asset",
    WALK  = "Asset/FBX/Mario2/animation/Walk.asset",
    RUN   = "Asset/FBX/Mario2/animation/Run.asset",
    JUMP1 = "Asset/FBX/Mario2/animation/Jump.asset",
    JUMP2 = "Asset/FBX/Mario2/animation/Jump2.asset",
    JUMP3 = "Asset/FBX/Mario2/animation/Jump3.asset",
}

-- ─────────────────────────────────────────────────────────────
-- 수치 설정
-- ─────────────────────────────────────────────────────────────
local WALK_SPEED = 4.0
local RUN_SPEED = 8.0
local JUMP_VELOCITY = 10.0
local GRAVITY = -25.0
local CHAIN_WINDOW = 0.5
local TURN_SPEED = 720.0

-- ─────────────────────────────────────────────────────────────
-- StateMachine Bool 이름
--   Lua는 입력/물리만 계산하고, 실제 애니메이션 전이는
--   엔진 C++ StateMachine의 Bool 조건으로 제어한다.
-- ─────────────────────────────────────────────────────────────
local BOOL = {
    IS_MOVING = "IsMoving",
    IS_RUNNING = "IsRunning",
    DO_JUMP1 = "DoJump1",
    DO_JUMP2 = "DoJump2",
    DO_JUMP3 = "DoJump3",
}

-- ─────────────────────────────────────────────────────────────
-- 컨트롤러 상태
-- ─────────────────────────────────────────────────────────────
local M = {
    initialized = false,
    pawn = nil,
    skelMesh = nil,
    anims = {},
    groundZ = 0.0,
    verticalVelocity = 0.0,
    isGrounded = true,
    jumpCount = 0,
    chainTimer = 0.0,
    facingYaw = 0.0,
    prevSpace = false,
    pendingJumpFlag = nil,
}

-- ─────────────────────────────────────────────────────────────
-- 유틸리티
-- ─────────────────────────────────────────────────────────────
local function VX(v) return v and (v.X or v.x) or 0 end
local function VY(v) return v and (v.Y or v.y) or 0 end
local function VZ(v) return v and (v.Z or v.z) or 0 end

-- 비정상 deltaTime이 들어오면 고정값으로 보정
local function SafeDt(dt)
    if not dt or dt <= 0 or dt > 0.2 then
        return 1 / 60
    end
    return dt
end

local function GetKey(k)
    if not Input or not Input.GetKey then
        return false
    end
    local ok, value = pcall(Input.GetKey, k)
    return ok and value == true
end

-- 애니메이션 시퀀스를 안전하게 로드
local function LoadSequence(path)
    local ok, handle = pcall(function()
        return Animation.LoadSequence(path)
    end)
    if ok and handle and handle.Valid then
        return handle
    end
    return nil
end

-- 엔진 StateMachine Bool 쓰기 helper
local function SetBool(name, value)
    if M.skelMesh then
        M.skelMesh:SetStateBool(name, value)
    end
end

-- 점프 트리거 Bool은 동시에 하나만 활성화되도록 관리
local function ResetJumpBools()
    SetBool(BOOL.DO_JUMP1, false)
    SetBool(BOOL.DO_JUMP2, false)
    SetBool(BOOL.DO_JUMP3, false)
end

-- ─────────────────────────────────────────────────────────────
-- AnimGraph / StateMachine 구성
--   LOCOMOTION 하나 아래에 WAIT / WALK / RUN을 묶고,
--   점프는 상위 노드에서 분리한다.
-- ─────────────────────────────────────────────────────────────
local function ConfigureStateMachine()
    return M.skelMesh:SetupStateMachineGraph({
        initial_state = "LOCOMOTION",
        states = {
            {
                name = "LOCOMOTION",
                looping = true,
                submachine = {
                    initial_state = "WAIT",
                    states = {
                        { name = "WAIT", sequence = M.anims.WAIT, looping = true },
                        { name = "WALK", sequence = M.anims.WALK, looping = true },
                        { name = "RUN", sequence = M.anims.RUN, looping = true },
                    },
                    transitions = {
                        { from = "WAIT", to = "WALK", conditions = {
                            { kind = "bool", name = BOOL.IS_MOVING, value = true },
                            { kind = "bool", name = BOOL.IS_RUNNING, value = false },
                        } },
                        { from = "WAIT", to = "RUN", conditions = {
                            { kind = "bool", name = BOOL.IS_MOVING, value = true },
                            { kind = "bool", name = BOOL.IS_RUNNING, value = true },
                        } },
                        { from = "WALK", to = "WAIT", conditions = {
                            { kind = "bool", name = BOOL.IS_MOVING, value = false },
                        } },
                        { from = "WALK", to = "RUN", conditions = {
                            { kind = "bool", name = BOOL.IS_RUNNING, value = true },
                        } },
                        { from = "RUN", to = "WAIT", conditions = {
                            { kind = "bool", name = BOOL.IS_MOVING, value = false },
                        } },
                        { from = "RUN", to = "WALK", conditions = {
                            { kind = "bool", name = BOOL.IS_MOVING, value = true },
                            { kind = "bool", name = BOOL.IS_RUNNING, value = false },
                        } },
                    }
                }
            },
            { name = "JUMP1", sequence = M.anims.JUMP1, looping = false },
            { name = "JUMP2", sequence = M.anims.JUMP2, looping = false },
            { name = "JUMP3", sequence = M.anims.JUMP3, looping = false },
        },
        transitions = {
            { from = "LOCOMOTION", to = "JUMP1", conditions = { { kind = "bool", name = BOOL.DO_JUMP1, value = true } } },
            { from = "LOCOMOTION", to = "JUMP2", conditions = { { kind = "bool", name = BOOL.DO_JUMP2, value = true } } },
            { from = "LOCOMOTION", to = "JUMP3", conditions = { { kind = "bool", name = BOOL.DO_JUMP3, value = true } } },

            { from = "JUMP1", to = "LOCOMOTION", blend_duration = 0.05, conditions = {
                { kind = "time", time = M.anims.JUMP1.PlayLength },
            } },
            { from = "JUMP2", to = "LOCOMOTION", blend_duration = 0.05, conditions = {
                { kind = "time", time = M.anims.JUMP2.PlayLength },
            } },
            { from = "JUMP3", to = "LOCOMOTION", blend_duration = 0.05, conditions = {
                { kind = "time", time = M.anims.JUMP3.PlayLength },
            } },
        }
    })
end

-- ─────────────────────────────────────────────────────────────
-- 초기화
-- ─────────────────────────────────────────────────────────────
local function Bootstrap()
    if M.initialized then
        return true
    end

    local ownerObj = owner or obj
    if not ownerObj then
        return false
    end

    if ownerObj.AsPawn then
        local ok, pawn = pcall(function()
            return ownerObj:AsPawn()
        end)
        if ok and pawn then
            M.pawn = pawn
        end
    end
    M.pawn = M.pawn or ownerObj

    if ownerObj.GetOrAddSkeletalMesh then
        local ok, skelMesh = pcall(function()
            return ownerObj:GetOrAddSkeletalMesh()
        end)
        if ok and skelMesh then
            M.skelMesh = skelMesh
        end
    end

    if not M.skelMesh then
        print("[MarioStateMachine] missing SkeletalMeshComponent")
        return false
    end

    M.skelMesh:SetSkeletalMesh(MESH_PATH)

    -- StateMachine에 사용할 모든 시퀀스를 미리 로드
    for key, path in pairs(ANIM_PATH) do
        local handle = LoadSequence(path)
        if not handle then
            print("[MarioStateMachine] failed to load animation: " .. key .. " -> " .. path)
            return false
        end
        M.anims[key] = handle
    end

    if not ConfigureStateMachine() then
        print("[MarioStateMachine] failed to build state machine graph")
        return false
    end

    M.groundZ = VZ(M.pawn.Location)
    ResetJumpBools()
    SetBool(BOOL.IS_MOVING, false)
    SetBool(BOOL.IS_RUNNING, false)

    M.initialized = true
    print("[MarioStateMachine] ready")
    return true
end

-- ─────────────────────────────────────────────────────────────
-- 입력 스냅샷
-- ─────────────────────────────────────────────────────────────
local function ReadInput()
    return {
        w = GetKey("W") or GetKey("UP"),
        a = GetKey("A") or GetKey("LEFT"),
        s = GetKey("S") or GetKey("DOWN"),
        d = GetKey("D") or GetKey("RIGHT"),
        shift = GetKey("SHIFT") or GetKey("LSHIFT"),
        space = GetKey("SPACE"),
    }
end

-- ─────────────────────────────────────────────────────────────
-- 수평 이동 + 캐릭터 회전
--   반환: hasMove(bool), isRunning(bool)
-- ─────────────────────────────────────────────────────────────
local function UpdateMovement(dt, input)
    local dx, dy = 0, 0
    if input.w then dx = dx + 1 end
    if input.s then dx = dx - 1 end
    if input.d then dy = dy + 1 end
    if input.a then dy = dy - 1 end

    if dx == 0 and dy == 0 then
        return false, false
    end

    local isRunning = input.shift
    local speed = isRunning and RUN_SPEED or WALK_SPEED
    local length = math.sqrt(dx * dx + dy * dy)
    local ndx = dx / length
    local ndy = dy / length

    local loc = M.pawn.Location
    M.pawn.Location = Vec(
        VX(loc) + ndx * speed * dt,
        VY(loc) + ndy * speed * dt,
        VZ(loc)
    )

    local targetYaw = math.deg(math.atan2(ndy, ndx))
    local diff = targetYaw - M.facingYaw
    while diff > 180 do diff = diff - 360 end
    while diff < -180 do diff = diff + 360 end

    local maxStep = TURN_SPEED * dt
    if math.abs(diff) <= maxStep then
        M.facingYaw = targetYaw
    else
        M.facingYaw = M.facingYaw + (diff > 0 and maxStep or -maxStep)
    end
    M.pawn.Rotation = Rot(0, M.facingYaw, 0)

    return true, isRunning
end

-- ─────────────────────────────────────────────────────────────
-- 수직 물리
--   중력 적용 + 착지 판정 + 점프 체인 창 열기
-- ─────────────────────────────────────────────────────────────
local function UpdateVertical(dt)
    if M.isGrounded then
        return
    end

    M.verticalVelocity = M.verticalVelocity + GRAVITY * dt

    local loc = M.pawn.Location
    local newZ = VZ(loc) + M.verticalVelocity * dt

    if newZ <= M.groundZ then
        newZ = M.groundZ
        M.isGrounded = true
        M.verticalVelocity = 0.0
        M.chainTimer = CHAIN_WINDOW
    end

    M.pawn.Location = Vec(VX(loc), VY(loc), newZ)
end

-- 점프 입력은 해당 jump Bool을 한 프레임만 true로 만들어
-- 엔진 StateMachine이 전이하도록 트리거한다.
local function TriggerJumpBool(flagName)
    ResetJumpBools()
    SetBool(flagName, true)
    M.pendingJumpFlag = flagName
end

-- ─────────────────────────────────────────────────────────────
-- 점프 처리
--   jumpCount 0 -> JUMP1
--   jumpCount 1 -> JUMP2
--   jumpCount 2 -> JUMP3 후 리셋
-- ─────────────────────────────────────────────────────────────
local function UpdateJump(dt, input)
    if M.chainTimer > 0 then
        M.chainTimer = M.chainTimer - dt
        if M.chainTimer <= 0 then
            M.chainTimer = 0
            M.jumpCount = 0
        end
    end

    local spaceDown = input.space and not M.prevSpace
    M.prevSpace = input.space

    if not (M.isGrounded and spaceDown) then
        return
    end

    M.isGrounded = false
    M.verticalVelocity = JUMP_VELOCITY
    M.chainTimer = 0.0

    if M.jumpCount == 2 then
        M.jumpCount = 0
        TriggerJumpBool(BOOL.DO_JUMP3)
    elseif M.jumpCount == 1 then
        M.jumpCount = 2
        TriggerJumpBool(BOOL.DO_JUMP2)
    else
        M.jumpCount = 1
        TriggerJumpBool(BOOL.DO_JUMP1)
    end
end

-- 이동/달리기 상태는 매 프레임 Bool로 동기화해서
-- LOCOMOTION 내부 sub-state가 WAIT/WALK/RUN 중 하나를 고른다.
local function UpdateStateMachineBools(hasMove, isRunning)
    SetBool(BOOL.IS_MOVING, hasMove)
    SetBool(BOOL.IS_RUNNING, hasMove and isRunning or false)
end

-- ─────────────────────────────────────────────────────────────
-- Lifecycle
-- ─────────────────────────────────────────────────────────────
function BeginPlay()
    Bootstrap()
end

function OnInput(deltaTime)
    if not Bootstrap() then
        return
    end

    -- 직전 프레임에 올린 점프 트리거 Bool은 여기서 내린다.
    -- 따라서 엔진 StateMachine은 최소 한 프레임 동안 true를 본다.
    if M.pendingJumpFlag then
        SetBool(M.pendingJumpFlag, false)
        M.pendingJumpFlag = nil
    end

    local dt = SafeDt(deltaTime)
    local input = ReadInput()

    local hasMove, isRunning = UpdateMovement(dt, input)
    UpdateVertical(dt)
    UpdateJump(dt, input)
    UpdateStateMachineBools(hasMove, isRunning)
end

function Tick(deltaTime)
    -- 입력/물리/상태 갱신은 OnInput에서 처리
end

function EndPlay()
    print("[MarioStateMachine] EndPlay")
end
