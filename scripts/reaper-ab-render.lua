local project = 0
local output = "/Volumes/DatenHub/Downloads/ReverseLab/TestArtifacts/REAPER"
local sourcePath = output .. "/reverselab-demo-source.aiff"

local function set_by_name(track, fx, wanted, normalized)
  for parameter = 0, reaper.TrackFX_GetNumParams(track, fx) - 1 do
    local _, name = reaper.TrackFX_GetParamName(track, fx, parameter, "")
    if name == wanted then
      reaper.TrackFX_SetParamNormalized(track, fx, parameter, normalized)
      return true
    end
  end
  return false
end

local function add_source_track(index, name)
  reaper.InsertTrackAtIndex(index, true)
  local track = reaper.GetTrack(project, index)
  reaper.GetSetMediaTrackInfo_String(track, "P_NAME", name, true)
  local source = reaper.PCM_Source_CreateFromFile(sourcePath)
  local item = reaper.AddMediaItemToTrack(track)
  local take = reaper.AddTakeToMediaItem(item)
  reaper.SetMediaItemTake_Source(take, source)
  reaper.SetMediaItemInfo_Value(item, "D_POSITION", 0.0)
  reaper.SetMediaItemInfo_Value(item, "D_LENGTH", 12.0)
  reaper.SetMediaItemInfo_Value(item, "B_LOOPSRC", 1)
  return track
end

local dry = add_source_track(0, "A DRY – no plug-in")
local wet = add_source_track(1, "B WET – ReverseLab split")
local reverse = reaper.TrackFX_AddByName(wet, "VST3: ReverseLab (Whykiki Audio)", false, 1)
assert(reverse >= 0, "ReverseLab VST3 could not be instantiated")

assert(set_by_name(wet, reverse, "Tempo Sync", 1.0))
assert(set_by_name(wet, reverse, "Link Left/Right", 0.0))
assert(set_by_name(wet, reverse, "Left Size", 0.0))       -- 1/32
assert(set_by_name(wet, reverse, "Right Size", 13.0 / 14.0)) -- 1 Bar
assert(set_by_name(wet, reverse, "Reverse Speed", 0.2))
assert(set_by_name(wet, reverse, "Crossfade", 0.08))
assert(set_by_name(wet, reverse, "Dry/Wet", 1.0))
assert(set_by_name(wet, reverse, "Feedback", 0.0))
assert(set_by_name(wet, reverse, "Stereo Offset", 0.5))
assert(set_by_name(wet, reverse, "Random", 0.0))

reaper.GetSet_LoopTimeRange(true, false, 0.0, 12.0, false)
reaper.GetSetProjectInfo(project, "RENDER_BOUNDSFLAG", 2, true)
reaper.GetSetProjectInfo_String(project, "RENDER_FILE", output, true)

reaper.SetMediaTrackInfo_Value(dry, "B_MUTE", 0)
reaper.SetMediaTrackInfo_Value(wet, "B_MUTE", 1)
reaper.GetSetProjectInfo_String(project, "RENDER_PATTERN", "reverselab-A-dry", true)
reaper.Main_OnCommand(42230, 0)

reaper.SetMediaTrackInfo_Value(dry, "B_MUTE", 1)
reaper.SetMediaTrackInfo_Value(wet, "B_MUTE", 0)
reaper.GetSetProjectInfo_String(project, "RENDER_PATTERN", "reverselab-B-wet-split", true)
reaper.Main_OnCommand(42230, 0)

reaper.Main_SaveProjectEx(project, output .. "/ReverseLab-AB-Comparison.rpp", 0)
local marker = assert(io.open(output .. "/ab-render-complete.txt", "w"))
marker:write("dry=reverselab-A-dry.wav\nwet=reverselab-B-wet-split.wav\n")
marker:close()
