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

reaper.InsertTrackAtIndex(0, true)
local track = reaper.GetTrack(project, 0)
reaper.GetSetMediaTrackInfo_String(track, "P_NAME", "AUDIBLE ReverseLab Demo", true)

local tone = reaper.TrackFX_AddByName(track, "JS: Tone Generator", false, 1)
local reverse = reaper.TrackFX_AddByName(track, "VST3: ReverseLab (Whykiki Audio)", false, 1)

if tone >= 0 and reverse >= 0 then
  set_by_name(track, reverse, "Tempo Sync", 1.0)
  set_by_name(track, reverse, "Left Size", 5.0 / 14.0)
  set_by_name(track, reverse, "Right Size", 5.0 / 14.0)
  set_by_name(track, reverse, "Reverse Speed", 0.4666667)
  set_by_name(track, reverse, "Crossfade", 0.16)
  set_by_name(track, reverse, "Dry/Wet", 0.72)
  set_by_name(track, reverse, "Feedback", 0.28)
  set_by_name(track, reverse, "Stereo Offset", 0.65)
  reaper.TrackFX_Show(track, reverse, 3)
end

reaper.GetSet_LoopTimeRange(true, false, 0.0, 16.0, false)
reaper.GetSetRepeat(1)
reaper.SetEditCurPos(0.0, true, false)
reaper.Main_SaveProjectEx(project, "/Volumes/DatenHub/Downloads/ReverseLab/TestArtifacts/REAPER/ReverseLab-Audible-Demo.rpp", 0)
reaper.OnPlayButton()
