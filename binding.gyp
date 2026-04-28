{
  "targets": [
    {
      "target_name": "grep_native",
      "sources": [
        "src/bm.c",
        "src/addon.c"
      ],
      "include_dirs": [
        "src"
      ],
      "defines": [
        "NAPI_VERSION=8"
      ],
      "conditions": [
        ["OS!='win'", {
          "cflags_c": ["-std=c11"],
          "configurations": {
            "Release": {
              "cflags_c": ["-O3"]
            },
            "Debug": {
              "cflags_c": ["-O0", "-g"]
            }
          }
        }],
        ["OS=='win'", {
          "msvs_settings": {
            "VCCLCompilerTool": {
              "DisableSpecificWarnings": ["4267", "4244"]
            }
          }
        }]
      ]
    }
  ]
}
