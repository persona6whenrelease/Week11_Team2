local Config = require("Game/MarioControllerConfig")

local Vec = FVector.new
local Rot = FRotator.new

local STATE = Config.STATE
local MOVEMENT = Config.MOVEMENT

local function VX(v) return v and (v.X or v.x) or 0 end
local function VY(v) return v and (v.Y or v.y) or 0 end
local function VZ(v) return v and (v.Z or v.z) or 0 end

local function SafeDt(dt)
    if not dt or dt <= 0 or dt > 0.2 then
        return 1 / 60
    end
    return dt
end

local function GetKey(keyName)
    if not Input or not Input.GetKey then
        return false
    end

    local ok, value = pcall(Input.GetKey, keyName)
    return ok and value == true
end

local function GetKeyDown(keyName, isPressed, wasPressed)
    if Input and Input.GetKeyDown then
        local ok, value = pcall(Input.GetKeyDown, keyName)
        if ok then
            return value == true
        end
    end

    return isPressed and not wasPressed
end

local function LoadSequence(path)
    local ok, handle = pcall(function()
        return Animation.LoadSequence(path)
    end)

    if ok and handle and handle.Valid then
        return handle
    end

    return nil
end

local function GetJumpVelocity(stage)
    local heights = MOVEMENT.JUMP_HEIGHTS
    if type(heights) == "table" and heights[stage] then
        local gravity = math.abs(MOVEMENT.GRAVITY)
        if gravity > 0 then
            return math.sqrt(2 * gravity * heights[stage])
        end
    end

    return 10.0
end

local SingleNodeBase = {}

function SingleNodeBase.Create(options)
    options = options or {}

    local tag = options.logTag or "MarioSingleNode"
    local getOwner = options.getOwner

    local M = {
        initialized = false,
        pawn = nil,
        skelMesh = nil,
        state = nil,
        anims = {},
        groundZ = 0.0,
        verticalVelocity = 0.0,
        isGrounded = true,
        jumpCount = 0,
        chainTimer = 0.0,
        facingYaw = 0.0,
        prevSpace = false,
    }

    local function PlayAnim(key, looping)
        local handle = M.anims[key]
        if not handle or not M.skelMesh then
            return
        end

        M.skelMesh:PlayAnimation(handle, looping ~= false)
    end

    local function EnterState(newState)
        if M.state == newState then
            return
        end

        M.state = newState
        local looping = (newState == STATE.IDLE or newState == STATE.WALK or newState == STATE.RUN)
        PlayAnim(newState, looping)
    end

    local function Bootstrap()
        if M.initialized then
            return true
        end

        local ownerObj = getOwner and getOwner() or owner or obj
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
            print("[" .. tag .. "] missing SkeletalMeshComponent")
            return false
        end

        M.skelMesh:SetSkeletalMesh(Config.MESH_PATH)

        for key, path in pairs(Config.ANIM_PATH.SINGLE_NODE) do
            local handle = LoadSequence(path)
            if not handle then
                print("[" .. tag .. "] failed to load animation: " .. key .. " -> " .. path)
                return false
            end
            M.anims[key] = handle
        end

        M.groundZ = VZ(M.pawn.Location)
        EnterState(STATE.IDLE)
        M.initialized = true
        print("[" .. tag .. "] ready")
        return true
    end

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
        local speed = isRunning and MOVEMENT.RUN_SPEED or MOVEMENT.WALK_SPEED
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

        local maxStep = MOVEMENT.TURN_SPEED * dt
        if math.abs(diff) <= maxStep then
            M.facingYaw = targetYaw
        else
            M.facingYaw = M.facingYaw + (diff > 0 and maxStep or -maxStep)
        end

        M.pawn.Rotation = Rot(0, M.facingYaw, 0)
        return true, isRunning
    end

    local function UpdateVertical(dt)
        if M.isGrounded then
            return
        end

        M.verticalVelocity = M.verticalVelocity + MOVEMENT.GRAVITY * dt

        local loc = M.pawn.Location
        local newZ = VZ(loc) + M.verticalVelocity * dt

        if newZ <= M.groundZ then
            newZ = M.groundZ
            M.isGrounded = true
            M.verticalVelocity = 0.0
            M.chainTimer = MOVEMENT.CHAIN_WINDOW
        end

        M.pawn.Location = Vec(VX(loc), VY(loc), newZ)
    end

    local function UpdateState(dt, input, hasMove, isRunning)
        if M.chainTimer > 0 then
            M.chainTimer = M.chainTimer - dt
            if M.chainTimer <= 0 then
                M.chainTimer = 0
                M.jumpCount = 0
            end
        end

        local spaceDown = GetKeyDown("SPACE", input.space, M.prevSpace)
        M.prevSpace = input.space

        if M.isGrounded and spaceDown then
            local nextJumpStage = math.min(M.jumpCount + 1, 3)
            M.isGrounded = false
            M.verticalVelocity = GetJumpVelocity(nextJumpStage)
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

    return {
        BeginPlay = function()
            Bootstrap()
        end,
        OnInput = function(deltaTime)
            if not Bootstrap() then
                return
            end

            local dt = SafeDt(deltaTime)
            local input = ReadInput()
            local hasMove, isRunning = UpdateMovement(dt, input)
            UpdateVertical(dt)
            UpdateState(dt, input, hasMove, isRunning)
        end,
        Tick = function(deltaTime)
        end,
        EndPlay = function()
            print("[" .. tag .. "] EndPlay")
        end,
    }
end

return SingleNodeBase
