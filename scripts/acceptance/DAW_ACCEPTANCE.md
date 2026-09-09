# DAW-Abnahme für SubLab808 und ReverseLab

Dieses Protokoll beschreibt noch auszuführende Prüfungen im DAW-Plugin. Ein
grüner Audio-Bericht bestätigt die ausgewerteten Exporte; die folgenden
Bedien- und Hörprüfungen benötigen eigene Ergebnisse. Alle Prüffälle beginnen
mit dem Status `nicht geprüft`. Die hier festgelegten Exportnamen gehören
zum Cubase-Protokoll. Andere Hosts als eigene Fälle dokumentieren; ihre
Exporte nicht allein für ein erkanntes Dateipaar als Cubase-Dateien ausgeben.

## Prüflauf vorbereiten

1. Ein separates Testprojekt anlegen. Pro Plugin den tatsächlich geladenen
   Build festhalten: Version, Commit, VST3-Pfad und SHA-256 der Plugin-Binärdatei.
   Dazu DAW-Version, Betriebssystem, Architektur, Samplerate, Puffergröße und
   Exportmodus dokumentieren. Vorher-/Nachher-Vergleiche verwenden denselben
   Build im selben Host mit denselben Einstellungen.
2. Für diese Fixtures 120 BPM, 4/4 und 48 kHz verwenden. Ohne weitere Effekte
   auf Spur oder Master exportieren, bei unveränderter Lautstärke und ohne
   Normalisierung, Dithering oder nachträgliches Beschneiden. Stereo-WAV mit
   32-Bit-Float verwenden. Alternative Einstellungen als eigenen Prüflauf
   erfassen; Echtzeit- und Offline-Export getrennt vergleichen. Jeweils nur
   die zu prüfende Spur exportieren; die zweite Plugin-Spur darf nicht in
   deren Referenz- oder Reload-Datei mitgerendert werden.
3. Die synthetischen Testdateien im ReverseLab-Repository erzeugen:

   ```sh
   python3 -B scripts/acceptance/audio_fixture.py generate
   ```

   `fixture-808-notes-120bpm.mid` auf eine SubLab808-Instrumentenspur legen.
   `fixture-transient-harmonic-sustain-48k.wav` auf eine Audiospur mit
   ReverseLab als Insert legen. Beide Dateien beginnen bei Projektzeit 0.
   Die MIDI-Datei enthält unterschiedliche Anschlagstärken, überlappende
   Noten, Pitchbend und All Notes Off. Die Audio-Datei enthält Transienten,
   harmonisches Material, gehaltene Töne und anschließende Stille.
4. Für jeden Preset-/Zustandsfall einen eigenen Ordner und eine eigene
   Projektkopie verwenden. Die vom Auswerter erkannten Dateinamen innerhalb
   dieses Ordners beibehalten. So überschreiben Exporte verschiedener Presets
   keine früheren Ergebnisse. SubLab- und ReverseLab-Exporte können gemeinsam
   in einem Fallordner liegen oder separat ausgewertet werden.
5. Das Preset in einer frischen Instanz wählen und den gewünschten Zustand
   bei gestopptem Transport speichern, bevor die Fixture abgespielt wurde.
   Abspielposition, Vorlauf und MIDI-Anfangszustand festhalten. Nach einer
   Probeaufführung vor dem Referenzexport wieder eine frische Instanz mit
   demselben Zustand herstellen; lediglich Zurückspulen setzt nicht jeden
   internen Klangzustand zurück.

## Audio nach Projekt-Neuladen vergleichen

1. Den unveränderten Ausgangszustand und die sichtbaren Parameter dokumentieren.
   Dann die Referenz ab Projektzeit 0 exportieren:

   | Plugin | Datei | Bereich |
   |---|---|---|
   | SubLab808 | `01-SubLab808-Cubase.wav` | 0 bis 16 Sekunden |
   | ReverseLab | `02-ReverseLab-Cubase.wav` | 0 bis 36 Sekunden |

   36 Sekunden sind hier der festgelegte ReverseLab-Prüfbereich. Der Auswerter
   erzwingt für ReverseLab gleiche Dateilängen, aber keine universelle
   Mindestdauer; deshalb den vereinbarten Exportbereich selbst kontrollieren.
2. Das Projekt schließen und die gespeicherte Projektkopie wieder öffnen.
   Das Preset nach dem Öffnen **nicht erneut auswählen**: geprüft wird der
   vom Host wiederhergestellte Zustand. Vor dem Export Namen, Änderungen,
   Parameter und Fenstergröße mit der Referenz vergleichen. Keine Noten oder
   Eingangssignale vorab abspielen.
3. Wieder bei Projektzeit 0 und identischem Vorlauf exportieren:

   | Plugin | Datei | Bereich |
   |---|---|---|
   | SubLab808 | `04-SubLab808-Cubase-after-reload-tail20s.wav` | 0 bis 36 Sekunden |
   | ReverseLab | `05-ReverseLab-Cubase-after-reload.wav` | 0 bis 36 Sekunden |

   SubLab vergleicht die ersten 16 Sekunden und prüft zusätzlich jede Probe
   zwischen Sekunde 30 und 36 auf Null. Die optionale SubLab-Datei
   `03-SubLab808-Cubase-tail2s.wav` umfasst 0 bis 18 Sekunden und erfordert
   denselben frischen Ausgangszustand. Sie ergänzt die Prüfung der ersten
   18 Sekunden nach Reload.
4. Die passende Recall-Anforderung ausführen. Beispiele vom Repository-Root:

   Zuerst beide Exporte vollständig abschließen. Während der Analyse die
   WAV-Dateien nicht neu exportieren oder verändern. Der Auswerter berechnet
   alle Messungen aus den je Datei einmal eingelesenen Bytes; ihre SHA-256-Werte
   stehen im Bericht. Das ersetzt keine gemeinsame atomare Aufnahme aller
   Dateien und keinen Nachweis über die tatsächlichen DAW-Schritte.

   ```sh
   python3 -B scripts/acceptance/analyse_host_audio.py /path/to/sub-case \
     --require-recall sublab808 --output-directory /path/to/sub-case/report-001
   python3 -B scripts/acceptance/analyse_host_audio.py /path/to/reverse-case \
     --require-recall reverselab --output-directory /path/to/reverse-case/report-001
   ```

   Für einen gemeinsamen Fallordner `--require-recall both` verwenden.
   Prozess-Exitcode, `run_id`, `analysis_completed`, Gesamtstatus und
   Abdeckung je Plugin erfassen. Fehlende, inkompatible oder beschädigte
   Exporte müssen als fehlgeschlagen behandelt werden.
5. Zunächst mit exakter Übereinstimmung prüfen. Eine Abweichung untersuchen:
   stimmen Beginn, Länge, Host-Modus, MIDI-Daten, Einstellungen und initialer
   Zustand überein? Änderungen an der Toleranz brauchen eine protokollierte
   Begründung und einen neuen Bericht. Der ursprüngliche Fehlerbericht bleibt
   erhalten. Den Klangvergleich zusätzlich hören.

## Eigene Prüfschwerpunkte je Plugin

Diese Auswahl deckt die unterschiedlichen Preset-Kategorien ab. Für jeden Fall
die obige Projekt-Wiederherstellung ausführen und das musikalische Verhalten
separat festhalten. Für eine vollständige Bank-Abnahme anschließend alle 64
Factory-Presets des jeweiligen Plugins einzeln abhören und als eigene Zeilen
protokollieren; die folgende Auswahl allein bestätigt keine ganze Bank.

| SubLab808-Preset | Zusätzliche Prüfung |
|---|---|
| Deep Foundation | Tiefe Grundnote und Ausklang; kein Hängenbleiben nach All Notes Off |
| Pure Floor | Mehrere tiefe MIDI-Noten und Anschlagstärken; tragfähiger, sauberer Grundton |
| Pocket Knock | Kurze und wiederholte Anschläge; klare, reproduzierbare Transienten |
| Portamento Silk | Überlappende und getrennte Noten; Glide und erneuter Anschlag unterscheiden sich hörbar |
| Tape Cushion | Weicher Anschlag und gerundete Obertöne bleiben nach Reload erhalten |
| Rust Engine | Drive und Output gemeinsam prüfen; Verzerrung bleibt beherrschbar, kein digitales Clipping |
| Neon Mono | Notenlänge, Note Off und Release; Rückkehr von Pitchbend in die Ausgangslage |
| Click Sub | Attack vor/nach Reload mit identischer Notenfolge; Unterschiede nicht aus den ersten 20 ms ausblenden |

SubLabs Click-Rauschfolge hängt vom vorherigen Abspielen ab. Für den
Reload-Vergleich muss die Notenfolge aus demselben Ausgangszustand beginnen.
Zusätzliche Live-Noten vor nur einem Export würden den Vergleich verändern.

| ReverseLab-Preset | Zusätzliche Prüfung |
|---|---|
| Clean Reverse | Segmentgrenzen und ursprüngliche Tonhöhe; genügend Eingangsmaterial vor dem ersten Reverse-Segment |
| Vocal Undertow | Dry/Wet-Verhältnis und Verständlichkeit des direkten Signals |
| Sixteenth Flip | Rhythmische Unterteilung bei 120 BPM; Tempoänderung als eigener Fall |
| Lateral Eighths | Link-/Stereo-Verhalten und Monosumme ohne versehentlich veränderte Pan-Einstellungen |
| Half Bar Lift | Langer Aufbau bis zum Reverse-Einsatz; gleiche Vorlaufzeit |
| Hold A Chord | Freeze-Aufnahme, Halten, Freigeben und erneutes Aufnehmen |
| Pixel Scramble | Fester Seed, identische Quelle und identischer Transportbeginn |
| Long Memory | Freie Millisekundenlänge; Tempoänderung verändert diese Einstellung nicht |

ReverseLab speichert Parameter einschließlich Seed und Freeze, aber keine
aufgenommenen Audiopuffer. Für einen exakten Audiovergleich deshalb beide
Exporte aus einem ungefüllten Puffer beginnen lassen und identisches neues
Eingangsmaterial zuführen. Ein vor dem Schließen bereits eingefrorener Klang
ist kein gespeichertes Audio-Sample im DAW-State.

Freeze zusätzlich in der laufenden Instanz testen: Material aufnehmen,
Transport stoppen/starten und einen Loop-Durchlauf prüfen. Die gehaltene
Aufnahme soll erhalten bleiben. Danach Freeze lösen, neues Material aufnehmen
und erneut halten. Nach echtem Projekt-Neuladen die wiederhergestellten
Parameter und die neue Aufnahme prüfen; hier nicht die Wiederherstellung des
alten Audiopuffers verlangen. Ergebnis und Hörbeobachtung getrennt vom
allgemeinen Recall-Vergleich erfassen.

## Eigene Presets und DAW-Bedienung

Für **jedes Plugin separat** mit eindeutig benannten Test-Presets durchführen:

| Fall | Ablauf und erwartetes Ergebnis |
|---|---|
| Save As | Factory-Preset ändern, unter neuem Testnamen speichern; in einer neu erzeugten Instanz dieses User-Preset ausdrücklich auswählen; Klang und Metadaten stimmen |
| Save | Eigenes Test-Preset ändern und speichern; erneutes Laden stellt die gespeicherte Änderung her |
| Ungespeicherte Änderung | Eigenes Preset ändern, nur das DAW-Projekt speichern und neu öffnen; geänderter Klang und Dirty-Anzeige bleiben erhalten |
| Fehlende Bibliotheksdatei | Nur die Datei des eigens angelegten Test-Presets gesichert auslagern; Projekt neu öffnen; der eingebettete DAW-Klang bleibt erhalten; Testdatei anschließend zurücklegen |
| Rename / Delete | Nur Test-Presets umbenennen/löschen; Factory bleibt geschützt, Löschen entfernt nicht den aktuellen Projektklang |
| Import / Export | Test-Preset exportieren und importieren; Werte stimmen, Namenskonflikt wird verständlich behandelt; fremdes Plugin-Format wird abgewiesen |
| Favoriten / Filter | Pro Plugin Favoriten setzen, suchen und Kategorien wechseln; Vor/Zurück folgt der sichtbaren Auswahl |
| Zwei Instanzen | Instanz A speichert eine Änderung; veraltetes Speichern in B darf sie nicht unbemerkt überschreiben |
| Dialog / Editor | Save-As- oder Exportdialog öffnen, Editor schließen und wieder öffnen; kein veralteter Dialog und keine Änderung an einer anderen Instanz |

Die Fälle mit eigenem gespeicherten und ungespeicherten Klang bekommen jeweils
eine eigene Audio-Recall-Prüfung. UI-Erfolg und Klang-Erfolg separat erfassen:
Ein identisches WAV beweist beispielsweise keinen korrekt angezeigten Namen.
Das ausdrückliche Laden in einer neuen Instanz prüft die Preset-Datei. Bei der
DAW-Projektwiederherstellung dagegen kein Preset nachladen: Sonst könnte ein
defekter oder fehlender Projektzustand unbemerkt durch Bibliothekswerte ersetzt
werden. Eine neu erzeugte Instanz muss nicht automatisch das zuletzt gespeicherte
User-Preset auswählen. Das erneute Öffnen nur des Editorfensters wiederum
erzeugt keine neue Plugin-Instanz und prüft keine dauerhafte Speicherung.

## Ergebnisvorlage

Pro Fall folgende Angaben speichern, beispielsweise als `TEST_RUN.md` neben
den WAV-Dateien und dem Bericht:

```text
Status: nicht geprüft | bestanden | fehlgeschlagen | blockiert
Prüfer / Datum:
Plugin / Version / Commit / Binärpfad / SHA-256:
DAW / Version / Betriebssystem / Architektur:
Samplerate / Puffergröße / Echtzeit oder Offline:
Preset / Kategorie / Factory oder User / gespeicherte Parameteränderungen:
Projektdatei / Zustand vor dem Referenzexport / dokumentierter Reload:
Eingangsdatei(en) / SHA-256 / Startzeit / Vorlauf / Exportbereiche:
Audio-Bericht / run_id / Exitcode / analysis_completed / passed:
Recall-Status je Plugin / Toleranz und gegebenenfalls Begründung:
Parameter- und Preset-Anzeige nach Reload:
Save-/Dialog-/Instanzprüfung:
Hörbeobachtungen / Screenshot- oder Audio-Belege:
Abweichung / reproduzierbare Schritte / nächste Maßnahme:
```

Einen Fall erst als bestanden markieren, wenn alle dafür vorgesehenen
Audio-, Bedien- und Hörprüfungen erfolgreich sind. Nicht ausgeführte
Prüfungen bleiben ausdrücklich offen. Eine fehlgeschlagene Auswertung mit
neuem `run_id` darf nicht durch einen älteren grünen Bericht ersetzt werden.
