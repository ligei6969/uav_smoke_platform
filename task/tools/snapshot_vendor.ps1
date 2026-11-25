param(
    [Parameter(Mandatory=$true)][string]$SdkRoot,
    [string]$TaskRoot = (Split-Path -Parent $PSScriptRoot)
)
$ErrorActionPreference='Stop'
$resolvedSdk=(Resolve-Path -LiteralPath $SdkRoot).Path
$resolvedTask=(Resolve-Path -LiteralPath $TaskRoot).Path
$destination=Join-Path $resolvedTask 'vendor\reference'
if(Test-Path -LiteralPath $destination){throw "Refusing to overwrite reference snapshot: $destination"}
$manifestPath=Join-Path $resolvedTask 'vendor\source_manifest.json'
if(Test-Path -LiteralPath $manifestPath){throw "Refusing to overwrite source manifest: $manifestPath"}
$relativeFiles=@(
 'samples\sample_c++\module_sample\liveview\test_liveview_entry.cpp',
 'samples\sample_c++\module_sample\liveview\test_liveview_entry.hpp',
 'samples\sample_c++\module_sample\liveview\test_liveview.cpp',
 'samples\sample_c++\module_sample\liveview\test_liveview.hpp',
 'samples\sample_c++\module_sample\liveview\dji_camera_stream_decoder.cpp',
 'samples\sample_c++\module_sample\liveview\dji_camera_stream_decoder.hpp',
 'samples\sample_c++\module_sample\liveview\dji_camera_image_handler.cpp',
 'samples\sample_c++\module_sample\liveview\dji_camera_image_handler.hpp',
 'samples\sample_c\module_sample\fc_subscription\test_fc_subscription.c',
 'samples\sample_c\module_sample\fc_subscription\test_fc_subscription.h',
 'samples\sample_c\module_sample\gimbal_manager\test_gimbal_manager.c',
 'samples\sample_c\module_sample\gimbal_manager\test_gimbal_manager.h',
 'samples\sample_c++\platform\linux\nvidia_jetson\hal\hal_usb_bulk.c',
 'samples\sample_c++\platform\linux\nvidia_jetson\hal\hal_usb_bulk.h',
 'samples\sample_c++\platform\linux\nvidia_jetson\hal\hal_uart.c',
 'samples\sample_c++\platform\linux\nvidia_jetson\hal\hal_uart.h',
 'samples\sample_c++\platform\linux\nvidia_jetson\CMakeLists.txt',
 'psdk_lib\include\dji_liveview.h',
 'psdk_lib\include\dji_fc_subscription.h',
 'LICENSE.txt','EULA.txt'
)
$externalFiles=@('samples\sample_c++\module_sample\liveview\data\best.onnx','psdk_lib\lib\aarch64-linux-gnu-gcc\libpayloadsdk.a')
foreach($relativeFile in ($relativeFiles + $externalFiles)){
 if(-not(Test-Path -LiteralPath (Join-Path $resolvedSdk $relativeFile) -PathType Leaf)){throw "Missing source: $relativeFile"}
}
New-Item -ItemType Directory -Path $destination | Out-Null
$manifest=@()
foreach($relativeFile in $relativeFiles){
 $original=Join-Path $resolvedSdk $relativeFile
 $copy=Join-Path $destination $relativeFile
 New-Item -ItemType Directory -Path (Split-Path -Parent $copy) -Force | Out-Null
 Copy-Item -LiteralPath $original -Destination $copy
 $hash=(Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash
 if((Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash -ne $hash){throw "Hash mismatch: $relativeFile"}
 $manifest+=@{path=$relativeFile.Replace('\','/');sha256=$hash;bytes=(Get-Item -LiteralPath $original).Length;role='reference_only_not_compiled'}
}
$assets=@()
foreach($relativeFile in $externalFiles){
 $original=Join-Path $resolvedSdk $relativeFile
 $assets+=@{path=$relativeFile.Replace('\','/');sha256=(Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash;bytes=(Get-Item -LiteralPath $original).Length;role='external_asset_not_copied'}
}
@{schema_version=1;source_root=$resolvedSdk;sources=$manifest;external_assets=$assets;note='Curated reference snapshot, not a complete transitive link dependency set; no app credentials copied.'} |
 ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
"Snapshot verified: $($manifest.Count) source/license files; $($assets.Count) external asset hashes"
