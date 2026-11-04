# Development-only encoding of the PNG frames captured by render-showcase.mjs.
$ErrorActionPreference='Stop'
Add-Type -AssemblyName PresentationCore
Add-Type -AssemblyName System.Drawing
$root=Split-Path -Parent $PSScriptRoot
foreach($style in @('matrix','matrix_rain')){
 $encoder=[Windows.Media.Imaging.GifBitmapEncoder]::new()
 $files=@(Get-ChildItem -LiteralPath (Join-Path $root ".work/showcase/$style") -Filter '*.png' | Sort-Object Name)
 $groups=[Collections.Generic.List[object]]::new()
 foreach($file in $files){
  $hash=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
  if($groups.Count -and $groups[$groups.Count-1].hash -eq $hash){$groups[$groups.Count-1].delay+=5}
  else{$groups.Add(@{file=$file.FullName;hash=$hash;delay=5})}
 }
 foreach($group in $groups){
  $image=[Windows.Media.Imaging.BitmapFrame]::Create([uri]$group.file)
  $metadata=[Windows.Media.Imaging.BitmapMetadata]::new('gif')
  $metadata.SetQuery('/grctlext/Delay',[uint16]$group.delay)
  $metadata.SetQuery('/grctlext/Disposal',[byte]2)
  $encoder.Frames.Add([Windows.Media.Imaging.BitmapFrame]::Create($image,$null,$metadata,$null))
 }
 $name=if($style -eq 'matrix_rain'){'matrix-rain.gif'}else{'matrix-classic.gif'}
 $destination=Join-Path $root "docs/media/$name"
 $stream=[IO.MemoryStream]::new()
 try{$encoder.Save($stream);$bytes=$stream.ToArray()}finally{$stream.Dispose()}
 # WPF omits frame-control extensions on some systems. Write GIF timing explicitly.
 $paletteBytes=if($bytes[10] -band 128){3*[Math]::Pow(2,($bytes[10] -band 7)+1)}else{0}
 $offset=[int](13+$paletteBytes)
 $loop=[byte[]](@(33,255,11)+[Text.Encoding]::ASCII.GetBytes('NETSCAPE2.0')+@(3,1,0,0,0))
 $encoded=[IO.MemoryStream]::new()
 try{
  $encoded.Write($bytes,0,$offset);$encoded.Write($loop,0,$loop.Length)
  $position=$offset;$frameIndex=0
  while($position -lt $bytes.Length){
   $start=$position;$kind=$bytes[$position];$position++
   if($kind -eq 59){$encoded.WriteByte(59);break}
   if($kind -eq 33){
    $label=$bytes[$position];$position++
    do{$size=$bytes[$position];$position++;$position+=$size}while($size)
    if($label -ne 249){$encoded.Write($bytes,$start,$position-$start)}
   }elseif($kind -eq 44){
    $packed=$bytes[$position+8];$position+=9
    if($packed -band 128){$position += [int](3*[Math]::Pow(2,($packed -band 7)+1))}
    $position++ # LZW minimum code size
    do{$size=$bytes[$position];$position++;$position+=$size}while($size)
    $delay=[int]$groups[$frameIndex].delay
    $control=[byte[]]@(33,249,4,8,($delay -band 255),(($delay -shr 8) -band 255),0,0)
    $encoded.Write($control,0,$control.Length);$encoded.Write($bytes,$start,$position-$start)
    $frameIndex++
   }else{throw "Unexpected GIF block: $kind"}
  }
  if($frameIndex -ne $groups.Count){throw 'GIF frame-control count mismatch'}
  $result=$encoded.ToArray()
 }finally{$encoded.Dispose()}
 [IO.File]::WriteAllBytes($destination,$result)
 $decoded=[Drawing.Image]::FromFile($destination)
 try{
  $count=$decoded.GetFrameCount([Drawing.Imaging.FrameDimension]::Time)
  if($count -ne $groups.Count){throw 'Encoded frame count mismatch'}
  if($decoded.Width -ne 960 -or $decoded.Height -ne 240){throw 'Encoded dimensions mismatch'}
  $delays=$decoded.GetPropertyItem(0x5100).Value;$duration=0
  for($i=0;$i -lt $delays.Length;$i+=4){$duration += [BitConverter]::ToInt32($delays,$i)*10}
  if($duration -ne $files.Count*50){throw 'Decoded GIF timing mismatch'}
  Write-Output "$name verified: $($files.Count) input frames, $count encoded frames, $($files.Count*50) ms, 960x240, $($result.Length) bytes."
 }finally{$decoded.Dispose()}
}
