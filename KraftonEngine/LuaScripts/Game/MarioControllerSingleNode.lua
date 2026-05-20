-- MarioControllerSingleNode.lua
-- Mario2 캐릭터용 SingleNode 애니메이션 컨트롤러
--
-- State 구조:
--   IDLE  <-> WALK <-> RUN
--   SPACE -> JUMP1 -> JUMP2 -> JUMP3
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
    IDLE  = "Asset/FBX/Mario2/animation/RoswellWait.asset",
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
-- State 열거
-- ─────────────────────────────────────────────────────────────
local STATE = {
    IDLE  = "IDLE",
    WALK  = "WALK",
    RUN   = "RUN",
    JUMP1 = "JUMP1",
    JUMP2 = "JUMP2",
    JUMP3 = "JUMP3",
}

-- ─────────────────────────────────────────────────────────────
-- 컨트롤러 상태
-- ─────────────────────────────────────────────────────────────
local M = {
    initialized = false,
    pawn = nil,
    skelMesh = nil,
    state = STATE.IDLE,
    anims = {},
    groundZ = 0.0,
    verticalVelocity = 0.0,
    isGrounded = true,
    jumpCount = 0,
    chainTimer = 0.0,
    facingYaw = 0.0,
    prevSpace = false,
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

-- SingleNode 방식이라 상태가 바뀔 때마다 해당 클립을 직접 재생
local function PlayAnim(key, looping)
    local handle = M.anims[key]
    if not handle or not M.skelMesh then
        return
    end
    M.skelMesh:PlayAnimation(handle, looping ~= false)
end

-- 상태 전환 + 루프 여부 결정
local function EnterState(newState)
    if M.state == newState then
        return
    end

    M.state = newState
    local looping = (newState == STATE.IDLE or newState == STATE.WALK or newState == STATE.RUN)
    PlayAnim(newState, looping)
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
        print("[MarioSingleNode] missing SkeletalMeshComponent")
        return false
    end

    M.skelMesh:SetSkeletalMesh(MESH_PATH)

    -- 사용할 모든 시퀀스를 미리 로드
    for key, path in pairs(ANIM_PATH) do
        local handle = LoadSequence(path)
        if not handle then
            print("[MarioSingleNode] failed to load animation: " .. key .. " -> " .. path)
            return false
        end
        M.anims[key] = handle
    end

    M.groundZ = VZ(M.pawn.Location)
    EnterState(STATE.IDLE)
    M.initialized = true
    print("[MarioSingleNode] ready")
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

-- ─────────────────────────────────────────────────────────────
-- 상태 결정
--   SingleNode 방식이라 Lua가 직접 상태를 고르고 재생한다.
-- ─────────────────────────────────────────────────────────────
local function UpdateState(dt, input, hasMove, isRunning)
    if M.chainTimer > 0 then
        M.chainTimer = M.chainTimer - dt
        if M.chainTimer <= 0 then
            M.chainTimer = 0
            M.jumpCount = 0
        end
    end

    local spaceDown = input.space and not M.prevSpace
    M.prevSpace = input.space

    if M.isGrounded and spaceDown then
        M.isGrounded = false
        M.verticalVelocity = JUMP_VELOCITY
        M.chainTimer = 0.0

        if M.jumpCount == 2 then
            M.jumpCount = 0
            EnterState(STATE.JUMP3)
        elseif M.jumpCount == 1 then
            M.jumpCount = 2
            EnterState(STATE.JUMP2)
        else
            M.jumpCount = 1
            EnterState(STATE.JUMP1)
        end
        return
    end

    -- 공중에서는 점프 애니메이션 유지
    if not M.isGrounded then
        return
    end

    if not hasMove then
        EnterState(STATE.IDLE)
    elseif isRunning then
        EnterState(STATE.RUN)
    else
        EnterState(STATE.WALK)
    end
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

    local dt = SafeDt(deltaTime)
    local input = ReadInput()
    local hasMove, isRunning = UpdateMovement(dt, input)
    UpdateVertical(dt)
    UpdateState(dt, input, hasMove, isRunning)
end

function Tick(deltaTime)
    -- 입력/물리/상태 갱신은 OnInput에서 처리
end

function EndPlay()
    print("[MarioSingleNode] EndPlay")
end
