-- MarioControllerAnimGraph.lua
-- Mario2 캐릭터용 애니메이션 State Machine 컨트롤러
--
-- State 구조:
--   IDLE  ←→ WALK ←→ RUN (SHIFT)
--   (지상 어디서든) SPACE → JUMP1 → JUMP2 → JUMP3
--   착지 후 CHAIN_WINDOW(0.5s) 이내 재점프 시 체인 이어짐
--
-- 이동: WASD 또는 방향키
-- 점프: SPACE
-- 달리기: SHIFT 홀드

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
local WALK_SPEED    = 4.0    -- 걷기 속도 (units/s)
local RUN_SPEED     = 8.0    -- 달리기 속도 (units/s)
local JUMP_VELOCITY = 10.0   -- 점프 초기 수직 속도 (units/s)
local GRAVITY       = -25.0  -- 중력 가속도 (units/s²)
local CHAIN_WINDOW  = 0.5    -- 착지 후 연속 점프 인식 시간 (s)
local TURN_SPEED    = 720.0  -- 캐릭터 회전 속도 (degrees/s)

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
    pawn        = nil,
    skelMesh    = nil,

    state = STATE.IDLE,
    anims = {},     -- { IDLE=handle, WALK=handle, ... }

    -- 수직 물리
    groundZ          = 0.0,
    verticalVelocity = 0.0,
    isGrounded       = true,

    -- 3단 점프 체인
    -- jumpCount: 현재 체인에서 누적된 점프 횟수 (0 = 첫 점프 대기)
    -- chainTimer: 착지 후 체인 인식 잔여 시간 (> 0 이면 체인 가능)
    jumpCount  = 0,
    chainTimer = 0.0,

    -- 캐릭터 수평 회전 (degrees)
    facingYaw = 0.0,

    -- SPACE 엣지 감지
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
    if not dt or dt <= 0 or dt > 0.2 then return 1 / 60 end
    return dt
end

local function GetKey(k)
    if not Input or not Input.GetKey then return false end
    local ok, v = pcall(Input.GetKey, k)
    return ok and v == true
end

-- ─────────────────────────────────────────────────────────────
-- 애니메이션 전환
--   SingleNode 재생 방식이라 상태별 클립을 직접 바꾼다.
-- ─────────────────────────────────────────────────────────────
local function PlayAnim(key, looping)
    local handle = M.anims[key]
    if not handle or not M.skelMesh then return end
    M.skelMesh:PlayAnimation(handle, looping ~= false)
end

local function EnterState(new)
    if M.state == new then return end
    M.state = new

    -- IDLE / WALK / RUN 은 루프, 점프는 1회 재생
    local loop = (new == STATE.IDLE or new == STATE.WALK or new == STATE.RUN)
    PlayAnim(new, loop)
end

-- ─────────────────────────────────────────────────────────────
-- 초기화
-- ─────────────────────────────────────────────────────────────
local function Bootstrap()
    if M.initialized then return true end

    local ownerObj = owner or obj
    if not ownerObj then return false end

    -- Pawn 레퍼런스 획득
    if ownerObj.AsPawn then
        local ok, p = pcall(function() return ownerObj:AsPawn() end)
        if ok and p then M.pawn = p end
    end
    M.pawn = M.pawn or ownerObj

    -- SkeletalMeshComponent 획득
    if ownerObj.GetOrAddSkeletalMesh then
        local ok, sm = pcall(function() return ownerObj:GetOrAddSkeletalMesh() end)
        if ok and sm then M.skelMesh = sm end
    end

    if not M.skelMesh then
        print("[Mario] ERROR: SkeletalMeshComponent를 찾을 수 없습니다.")
        return false
    end

    -- 메시 설정
    M.skelMesh:SetSkeletalMesh(MESH_PATH)

    -- 애니메이션 로드
    --   사용할 모든 시퀀스를 미리 메모리에 올린다.
    local loadOk = true
    for key, path in pairs(ANIM_PATH) do
        local ok, handle = pcall(function() return Animation.LoadSequence(path) end)
        if ok and handle and handle.Valid then
            M.anims[key] = handle
        else
            print("[Mario] 애니메이션 로드 실패: " .. key .. " ← " .. path)
            loadOk = false
        end
    end

    if not loadOk then
        print("[Mario] 일부 애니메이션 로드 실패 (경로 확인 필요)")
    end

    -- 초기 지면 높이 기록
    M.groundZ = VZ(M.pawn.Location)

    EnterState(STATE.IDLE)
    M.initialized = true
    print("[Mario] 초기화 완료 (groundZ=" .. tostring(M.groundZ) .. ")")
    return true
end

-- ─────────────────────────────────────────────────────────────
-- 입력 스냅샷
-- ─────────────────────────────────────────────────────────────
local function ReadInput()
    return {
        w     = GetKey("W")     or GetKey("UP"),
        a     = GetKey("A")     or GetKey("LEFT"),
        s     = GetKey("S")     or GetKey("DOWN"),
        d     = GetKey("D")     or GetKey("RIGHT"),
        shift = GetKey("SHIFT") or GetKey("LSHIFT"),
        space = GetKey("SPACE"),
    }
end

-- ─────────────────────────────────────────────────────────────
-- 수평 이동 + 캐릭터 회전
--   반환: hasMove(bool), isSprinting(bool)
-- ─────────────────────────────────────────────────────────────
local function UpdateMovement(dt, inp)
    local dx, dy = 0, 0
    if inp.w then dx = dx + 1 end
    if inp.s then dx = dx - 1 end
    if inp.d then dy = dy + 1 end
    if inp.a then dy = dy - 1 end

    if dx == 0 and dy == 0 then return false, false end

    local isSprint = inp.shift
    local speed    = isSprint and RUN_SPEED or WALK_SPEED

    -- 대각선 이동 정규화
    local len = math.sqrt(dx * dx + dy * dy)
    local ndx = dx / len
    local ndy = dy / len

    local loc = M.pawn.Location
    M.pawn.Location = Vec(
        VX(loc) + ndx * speed * dt,
        VY(loc) + ndy * speed * dt,
        VZ(loc)
    )

    -- 이동 방향으로 캐릭터 부드럽게 회전
    local targetYaw = math.deg(math.atan2(ndy, ndx))
    local diff = targetYaw - M.facingYaw
    while diff >  180 do diff = diff - 360 end
    while diff < -180 do diff = diff + 360 end
    local maxStep = TURN_SPEED * dt
    if math.abs(diff) <= maxStep then
        M.facingYaw = targetYaw
    else
        M.facingYaw = M.facingYaw + (diff > 0 and maxStep or -maxStep)
    end
    M.pawn.Rotation = Rot(0, M.facingYaw, 0)

    return true, isSprint
end

-- ─────────────────────────────────────────────────────────────
-- 수직 물리: 중력 적용 + 착지 감지
-- ─────────────────────────────────────────────────────────────
local function UpdateVertical(dt)
    if M.isGrounded then return end

    M.verticalVelocity = M.verticalVelocity + GRAVITY * dt

    local loc  = M.pawn.Location
    local newZ = VZ(loc) + M.verticalVelocity * dt

    if newZ <= M.groundZ then
        -- 착지 처리
        newZ               = M.groundZ
        M.isGrounded       = true
        M.verticalVelocity = 0

        -- 착지 순간 체인 점프 인식 창 개방
        M.chainTimer = CHAIN_WINDOW
    end

    M.pawn.Location = Vec(VX(loc), VY(loc), newZ)
end

-- ─────────────────────────────────────────────────────────────
-- State Machine 전이
--
-- 3단 점프 체인 규칙:
--   jumpCount 0 → JUMP1 (jumpCount=1)
--   착지 후 CHAIN_WINDOW 내 → JUMP2 (jumpCount=2)
--   착지 후 CHAIN_WINDOW 내 → JUMP3 (jumpCount=0, 체인 리셋)
--   창 만료 시 jumpCount = 0 으로 강제 리셋
-- ─────────────────────────────────────────────────────────────
local function UpdateState(dt, inp, hasMove, isSprint)
    -- 체인 타이머 차감 (점프 중에는 chainTimer=0 이므로 실질적으로 지상에서만 진행)
    if M.chainTimer > 0 then
        M.chainTimer = M.chainTimer - dt
        if M.chainTimer <= 0 then
            M.chainTimer = 0
            M.jumpCount  = 0
        end
    end

    -- SPACE 엣지 감지 (누른 순간 1프레임만 true)
    local spaceDown = inp.space and not M.prevSpace
    M.prevSpace = inp.space

    -- ── 점프 트리거 ────────────────────────────────────────
    if M.isGrounded and spaceDown then
        M.isGrounded       = false
        M.verticalVelocity = JUMP_VELOCITY
        M.chainTimer       = 0  -- 공중에서는 체인 창 닫음; 착지 시 재개방

        if M.jumpCount == 2 then
            -- 3단 점프 (JUMP3) 후 체인 리셋
            M.jumpCount = 0
            EnterState(STATE.JUMP3)
        elseif M.jumpCount == 1 then
            -- 2단 점프 (JUMP2)
            M.jumpCount = 2
            EnterState(STATE.JUMP2)
        else
            -- 1단 점프 (JUMP1) — 체인 시작
            M.jumpCount = 1
            EnterState(STATE.JUMP1)
        end
        return
    end

    -- ── 공중에서는 지상 State로 전이 금지 ──────────────────
    -- 점프 애니메이션이 끝나더라도 착지 전까지는 유지한다.
    if not M.isGrounded then return end

    -- ── 지상 State 결정 ────────────────────────────────────
    if not hasMove then
        EnterState(STATE.IDLE)
    elseif isSprint then
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
    if not Bootstrap() then return end
    local dt = SafeDt(deltaTime)

    local inp = ReadInput()

    -- 순서 중요: 수평 이동 → 수직 물리 → 상태 전이
    local hasMove, isSprint = UpdateMovement(dt, inp)
    UpdateVertical(dt)
    UpdateState(dt, inp, hasMove, isSprint)
end

function Tick(deltaTime)
    -- 물리/입력은 OnInput에서 처리; Tick은 시각 효과 등 확장용
end

function EndPlay()
    print("[Mario] EndPlay")
end
