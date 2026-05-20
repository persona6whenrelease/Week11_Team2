local Config = require("Game/MarioControllerConfig")
local Base = require("Game/MarioControllerAnimGraphBase")

local Controller = Base.Create({
    logTag = "MarioStateMachineNoBlend",
    blendProfile = Config.BLEND.NO_BLEND,
    getOwner = function()
        return owner or obj
    end,
})

function BeginPlay()
    Controller.BeginPlay()
end

function OnInput(deltaTime)
    Controller.OnInput(deltaTime)
end

function Tick(deltaTime)
    Controller.Tick(deltaTime)
end

function EndPlay()
    Controller.EndPlay()
end
