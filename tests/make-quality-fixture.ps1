Add-Type -AssemblyName System.Speech
$synth=New-Object System.Speech.Synthesis.SpeechSynthesizer
$voice=$synth.GetInstalledVoices() | Where-Object {$_.VoiceInfo.Culture.Name -eq 'ru-RU'} | Select-Object -First 1
if(-not $voice){throw 'No local Russian voice'}
$synth.SelectVoice($voice.VoiceInfo.Name)
$format=New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(16000,[System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,[System.Speech.AudioFormat.AudioChannel]::Mono)
$destination=Join-Path $PSScriptRoot '../.work/quality-russian.wav'
$synth.SetOutputToWaveFile([IO.Path]::GetFullPath($destination),$format)
$synth.SpeakSsml(@'
<speak version="1.0" xmlns="http://www.w3.org/2001/10/synthesis" xml:lang="ru-RU">
Я не говорил, что нужно выключить микрофон. Я попросил уменьшить громкость музыки.
<break time="1200ms"/>
Поезд отправляется в половине восьмого вечера, а встреча начнётся завтра в девять утра.
<break time="1200ms"/>
Если соединение прервётся, запись продолжится локально, и ни один файл не будет отправлен в облако.
<break time="1200ms"/>
Разработчик устранил переполнение очереди, не изменяя громкость и тембр голоса.
<break time="1200ms"/>
</speak>
'@)
$synth.Dispose()
Write-Output $destination
