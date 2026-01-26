Get-ChildItem -Path .\res\shaders\ -File -Recurse -include *.ep.slang -exclude *.spv | ForEach-Object {
    $sourcePath = $_.fullname
    $targetPath = "$($_.fullname).spv"
    slangc.exe -profile glsl_460 -capability spvShaderNonUniformEXT -target spirv -emit-spirv-directly -force-glsl-scalar-layout -fvk-use-entrypoint-name -I .\res\shaders\ -entry vertexMain -entry fragmentMain -g -DDEBUG=1 -o $targetPath $sourcePath
}

# Compute Shaders
Get-ChildItem -Path .\res\shaders\ -File -Recurse -include *.cmp.slang -exclude *.spv | ForEach-Object {
    $sourcePath = $_.fullname
    $targetPath = "$($_.fullname).spv"
    slangc.exe -profile glsl_460 -target spirv -emit-spirv-directly -force-glsl-scalar-layout -fvk-use-entrypoint-name -I .\res\shaders\ -entry cmpMain -g -DDEBUG=1 -o $targetPath $sourcePath
}
