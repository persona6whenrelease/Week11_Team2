local Base = require("Game/MarioControllerSingleNodeBase")

local Controller = Base.Create({
    logTag = "MarioOnlyLua",
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
