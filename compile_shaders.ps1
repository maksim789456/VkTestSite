Get-ChildItem -Path .\res\shaders\ -File -Recurse -exclude *.spv | ForEach-Object {
    $sourcePath = $_.fullname
    $targetPath = "$($_.fullname).spv"
    slangc.exe -profile glsl_460 -capability spvShaderNonUniformEXT -target spirv -emit-spirv-directly -force-glsl-scalar-layout -fvk-use-entrypoint-name -entry vertexMain -entry fragmentMain -g -o $targetPath $sourcePath
}
