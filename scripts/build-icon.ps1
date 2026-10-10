# Rasterize the approved SVG into a multi-resolution Windows icon and PNG preview.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$destination = Join-Path (Split-Path -Parent $PSScriptRoot) 'assets\callayers.ico'
[xml]$art = Get-Content (Join-Path (Split-Path -Parent $PSScriptRoot) 'assets\callayers-mark.svg') -Raw
# The mark uses only absolute move, line, cubic Bezier, and close commands.
function Convert-MarkPath([string]$data) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $tokens = [regex]::Matches($data, '[MLCZ]|-?\d+(?:\.\d+)?')
    $i = 0; $x = 0.0; $y = 0.0
    while ($i -lt $tokens.Count) {
        $command = $tokens[$i++].Value
        switch ($command) {
            'M' { $x = [float]::Parse($tokens[$i++].Value,[cultureinfo]::InvariantCulture); $y = [float]::Parse($tokens[$i++].Value,[cultureinfo]::InvariantCulture); $path.StartFigure() }
            'L' {
                $nx = [float]::Parse($tokens[$i++].Value,[cultureinfo]::InvariantCulture); $ny = [float]::Parse($tokens[$i++].Value,[cultureinfo]::InvariantCulture)
                $path.AddLine([float]$x,[float]$y,$nx,$ny); $x = $nx; $y = $ny
            }
            'C' {
                $v = @(for ($j = 0; $j -lt 6; $j++) { [float]::Parse($tokens[$i++].Value,[cultureinfo]::InvariantCulture) })
                $path.AddBezier([float]$x,[float]$y,$v[0],$v[1],$v[2],$v[3],$v[4],$v[5]); $x = $v[4]; $y = $v[5]
            }
            'Z' { $path.CloseFigure() }
            default { throw "Unsupported mark path command: $command" }
        }
    }
    return $path
}
$markPaths = @($art.svg.g.path | ForEach-Object { Convert-MarkPath $_.d })
$tile = New-Object System.Drawing.Drawing2D.GraphicsPath
$tile.AddArc(0,0,40,40,180,90); $tile.AddArc(60,0,40,40,270,90)
$tile.AddArc(60,60,40,40,0,90); $tile.AddArc(0,60,40,40,90,90); $tile.CloseFigure()
$tileBrush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml($art.svg.rect.fill))
$images = @()
foreach ($size in @(16,24,32,48,64,128,256)) {
    $bitmap = New-Object System.Drawing.Bitmap($size,$size)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $graphics.ScaleTransform($size/100.0,$size/100.0)
    $graphics.FillPath($tileBrush,$tile)
    foreach ($mark in $markPaths) { $graphics.FillPath([System.Drawing.Brushes]::White,$mark) }
    $stream = New-Object System.IO.MemoryStream
    $bitmap.Save($stream,[System.Drawing.Imaging.ImageFormat]::Png)
    $images += ,@($size,$stream.ToArray())
    if ($size -eq 256) { $bitmap.Save([System.IO.Path]::ChangeExtension($destination,'.png'),[System.Drawing.Imaging.ImageFormat]::Png) }
    $stream.Dispose(); $graphics.Dispose(); $bitmap.Dispose()
}
$tile.Dispose(); $tileBrush.Dispose(); foreach ($mark in $markPaths) { $mark.Dispose() }
$file = [System.IO.File]::Create($destination)
$writer = New-Object System.IO.BinaryWriter($file)
$writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$images.Count)
$offset = 6 + 16*$images.Count
foreach ($item in $images) {
    $dimension = if ($item[0] -eq 256) { 0 } else { $item[0] }
    $writer.Write([byte]$dimension); $writer.Write([byte]$dimension); $writer.Write([byte]0); $writer.Write([byte]0)
    $writer.Write([uint16]1); $writer.Write([uint16]32); $writer.Write([uint32]$item[1].Length); $writer.Write([uint32]$offset)
    $offset += $item[1].Length
}
foreach ($item in $images) { $writer.Write([byte[]]$item[1]) }
$writer.Dispose(); $file.Dispose()
