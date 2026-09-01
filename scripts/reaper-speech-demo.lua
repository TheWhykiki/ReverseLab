local project = 0

local function set_by_name(track, fx, wanted, normalized)
  for parameter = 0, reaper.TrackFX_GetNumParams(track, fx) - 1 do
    local _, name = reaper.TrackFX_GetParamName(track, fx, parameter, "")
    if name == wanted then
      reaper.TrackFX_SetParamNormalized(track, fx, parameter, normalized)
      return
    end
  end
end

for index = 0, reaper.CountTracks(project) - 1 do
  reaper.SetMediaTrackInfo_Value(reaper.GetTrack(project, index), "B_MUTE", 1)
end

reaper.InsertTrackAtIndex(reaper.CountTracks(project), true)
local track = reaper.GetTrack(project, reaper.CountTracks(project) - 1)
reaper.GetSetMediaTrackInfo_String(track, "P_NAME", "HEAR ReverseLab – Speech", true)

local source = reaper.PCM_Source_CreateFromFile("/Volumes/DatenHub/Downloads/ReverseLab/TestArtifacts/REAPER/reverselab-demo-source.aiff")
local item = reaper.AddMediaItemToTrack(track)
local take = reaper.AddTakeToMediaItem(item)
reaper.SetMediaItemTake_Source(take, source)
reaper.SetMediaItemInfo_Value(item, "D_POSITION", 0.0)
reaper.SetMediaItemInfo_Value(item, "D_LENGTH", 16.0)
reaper.SetMediaItemInfo_Value(item, "B_LOOPSRC", 1)

local reverse = reaper.TrackFX_AddByName(track, "VST3: ReverseLab (Whykiki Audio)", false, 1)
if reverse >= 0 then
  set_by_name(track, reverse, "Tempo Sync", 1.0)
  set_by_name(track, reverse, "Link Left/Right", 0.0)
  -- Deliberately extreme L/R values: this is an audibility test, not a subtle preset.
  set_by_name(track, reverse, "Left Size", 0.0)
  set_by_name(track, reverse, "Right Size", 13.0 / 14.0)
  set_by_name(track, reverse, "Reverse Speed", 0.2)
  set_by_name(track, reverse, "Crossfade", 0.08)
  set_by_name(track, reverse, "Dry/Wet", 1.0)
  set_by_name(track, reverse, "Feedback", 0.0)
  set_by_name(track, reverse, "Random", 0.0)
  set_by_name(track, reverse, "Stereo Offset", 0.5)
  reaper.TrackFX_Show(track, reverse, 3)
end

reaper.GetSet_LoopTimeRange(true, false, 0.0, 16.0, false)
reaper.GetSetRepeat(1)
reaper.SetEditCurPos(0.0, true, false)
reaper.Main_SaveProjectEx(project, "/Volumes/DatenHub/Downloads/ReverseLab/TestArtifacts/REAPER/ReverseLab-Audible-Demo.rpp", 0)
reaper.OnPlayButton()
reaper.UpdateArrange()
