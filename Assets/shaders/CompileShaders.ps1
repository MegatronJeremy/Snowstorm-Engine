# powershell
param(
  [Parameter(Mandatory=$true)][string]$Input,
  [Parameter(Mandatory=$true)][string]$Glslc
)

$content = Get-Content $Input -Raw

function ExtractStage([string]$stage) {
  $pattern = "(?s)#type\s+$stage\s*\r?\n(.*?)(?=\r?\n#type\s+|$)"
  $m = [regex]::Match($content, $pattern)
  if (-not $m.Success) { throw "Stage '$stage' not found in $Input" }
  return $m.Groups[1].Value
}

$base = [IO.Path]::Combine([IO.Path]::GetDirectoryName($Input),
          [IO.Path]::GetFileNameWithoutExtension($Input))

$vert = ExtractStage "vertex"
$frag = ExtractStage "fragment"

$vertPath = "$base.vert"
$fragPath = "$base.frag"

Set-Content -Path $vertPath -Value $vert -NoNewline
Set-Content -Path $fragPath -Value $frag -NoNewline

& $Glslc $vertPath -o "$vertPath.spv"
if ($LASTEXITCODE -ne 0) { throw "glslc failed compiling $vertPath" }

& $Glslc $fragPath -o "$fragPath.spv"
if ($LASTEXITCODE -ne 0) { throw "glslc failed compiling $fragPath" }