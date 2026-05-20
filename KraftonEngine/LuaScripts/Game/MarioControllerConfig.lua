local Config = {}

Config.MESH_PATH = "Asset/FBX/Mario2/Mario2.asset"

Config.ANIM_PATH = {
    SINGLE_NODE = {
        IDLE  = "Asset/FBX/Mario2/animation/RoswellWait.asset",
        WALK  = "Asset/FBX/Mario2/animation/Walk.asset",
        RUN   = "Asset/FBX/Mario2/animation/Run.asset",
        JUMP1 = "Asset/FBX/Mario2/animation/Jump.asset",
        JUMP2 = "Asset/FBX/Mario2/animation/Jump2.asset",
        JUMP3 = "Asset/FBX/Mario2/animation/Jump3.asset",
    },
    ANIM_GRAPH = {
        WAIT  = "Asset/FBX/Mario2/animation/RoswellWait.asset",
        WALK  = "Asset/FBX/Mario2/animation/Walk.asset",
        RUN   = "Asset/FBX/Mario2/animation/Run.asset",
        JUMP1 = "Asset/FBX/Mario2/animation/Jump.asset",
        JUMP2 = "Asset/FBX/Mario2/animation/Jump2.asset",
        JUMP3 = "Asset/FBX/Mario2/animation/Jump3.asset",
    },
}

Config.MOVEMENT = {
    WALK_SPEED = 4.0,
    RUN_SPEED = 8.0,
    JUMP_HEIGHTS = {
        2.0,
        4.0,
        6.0,
    },
    GRAVITY = -25.0,
    CHAIN_WINDOW = 0.5,
    TURN_SPEED = 720.0,
}

Config.STATE = {
    IDLE  = "IDLE",
    WALK  = "WALK",
    RUN   = "RUN",
    JUMP1 = "JUMP1",
    JUMP2 = "JUMP2",
    JUMP3 = "JUMP3",
}

Config.BOOL = {
    IS_MOVING = "IsMoving",
    IS_RUNNING = "IsRunning",
    IS_GROUNDED = "IsGrounded",
    DO_JUMP1 = "DoJump1",
    DO_JUMP2 = "DoJump2",
    DO_JUMP3 = "DoJump3",
}

Config.BLEND = {
    DEFAULT = {
        locomotion = {
            wait_to_walk = 0.3,
            wait_to_run = 0.3,
            walk_to_wait = 0.3,
            walk_to_run = 0.3,
            run_to_wait = 0.3,
            run_to_walk = 0.3,
        },
        jump = {
            enter = 0.08,
            chain = 0.08,
            exit = 0.08,
        },
    },
    NO_BLEND = {
        locomotion = {
            wait_to_walk = 0.2,
            wait_to_run = 0.0,
            walk_to_wait = 0.0,
            walk_to_run = 0.0,
            run_to_wait = 0.0,
            run_to_walk = 0.0,
        },
        jump = {
            enter = 0.0,
            chain = 0.0,
            exit = 0.0,
        },
    },
}

local function DeepCopy(value)
    if type(value) ~= "table" then
        return value
    end

    local copy = {}
    for key, nested in pairs(value) do
        copy[key] = DeepCopy(nested)
    end
    return copy
end

function Config.BuildBlendProfile(overrides)
    local profile = DeepCopy(Config.BLEND.DEFAULT)

    if type(overrides) ~= "table" then
        return profile
    end

    for groupName, groupValues in pairs(overrides) do
        if type(groupValues) == "table" and type(profile[groupName]) == "table" then
            for key, value in pairs(groupValues) do
                profile[groupName][key] = value
            end
        else
            profile[groupName] = DeepCopy(groupValues)
        end
    end

    return profile
end

return Config
