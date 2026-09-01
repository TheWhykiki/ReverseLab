local project = 0
local root = "/private/tmp/reaper-reverselab"
local log = assert(io.open(root .. "/stress-result.txt", "w"))
local instances = 32
local created = 0

local function set_by_name(track, fx, wanted, normalized)
  local count = reaper.TrackFX_GetNumParams(track, fx)
  for parameter = 0, count - 1 do
    local _, name = reaper.TrackFX_GetParamName(track, fx, parameter, "")
    if name == wanted then
      reaper.TrackFX_SetParamNormalized(track, fx, parameter, normalized)
      return true
    end
  end
  return false
end

for index = 0, instances - 1 do
  reaper.InsertTrackAtIndex(index, true)
  local track = reaper.GetTrack(project, index)
  reaper.GetSetMediaTrackInfo_String(track, "P_NAME", "ReverseLab Stress " .. tostring(index + 1), true)
  reaper.SetMediaTrackInfo_Value(track, "D_VOL", 0.01)
  local tone = reaper.TrackFX_AddByName(track, "JS: Tone Generator", false, 1)
  local reverse = reaper.TrackFX_AddByName(track, "VST3: ReverseLab (Whykiki Audio)", false, 1)
  if tone >= 0 and reverse >= 0 then
    created = created + 1
    set_by_name(track, reverse, "Tempo Sync", index % 2)
    set_by_name(track, reverse, "Link Left/Right", 0.0)
    set_by_name(track, reverse, "Left Free Time", (index % 7) / 6.0)
    set_by_name(track, reverse, "Right Free Time", ((index + 3) % 7) / 6.0)
    set_by_name(track, reverse, "Reverse Speed", (index % 5) / 4.0)
    set_by_name(track, reverse, "Crossfade", (index % 4) / 3.0)
    set_by_name(track, reverse, "Dry/Wet", 1.0)
    set_by_name(track, reverse, "Feedback", index % 3 == 0 and 1.0 or 0.35)
    set_by_name(track, reverse, "Random", index % 4 == 0 and 1.0 or 0.2)
    set_by_name(track, reverse, "Stereo Offset", (index % 9) / 8.0)
  end
end

local first = reaper.GetTrack(project, 0)
local firstFx = 1
local parameterCount = first and reaper.TrackFX_GetNumParams(first, firstFx) or -1
log:write("requested_instances=" .. tostring(instances) .. "\n")
log:write("created_instances=" .. tostring(created) .. "\n")
log:write("parameters_per_instance=" .. tostring(parameterCount) .. "\n")

reaper.GetSet_LoopTimeRange(true, false, 0.0, 2.0, false)
reaper.GetSetProjectInfo(project, "RENDER_BOUNDSFLAG", 2, true)
reaper.GetSetProjectInfo_String(project, "RENDER_FILE", root, true)
reaper.GetSetProjectInfo_String(project, "RENDER_PATTERN", "reverselab-stress-render", true)
reaper.Main_SaveProjectEx(project, root .. "/ReverseLab-Stress-Test.rpp", 0)
log:write("project_saved=1\n")
log:close()

reaper.Main_OnCommand(42230, 0)
local completed = assert(io.open(root .. "/stress-complete.txt", "w"))
completed:write("render_action_completed=1\n")
completed:close()
