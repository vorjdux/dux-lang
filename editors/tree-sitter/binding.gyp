{
  "targets": [
    {
      "target_name": "tree_sitter_dux_binding",
      "include_dirs": [
        "<!(node -e \"require('nan')\")",
        "node_modules/tree-sitter/src"
      ],
      "sources": [
        "src/parser.c",
        "bindings/node/binding.cc"
      ],
      "cflags_c": [
        "-std=c99",
        "-fvisibility=hidden"
      ],
      "conditions": [
        [
          "OS == 'mac'",
          {
            "xcode_settings": {
              "MACOSX_DEPLOYMENT_TARGET": "10.13",
              "OTHER_CFLAGS": [
                "-std=c99",
                "-fvisibility=hidden"
              ]
            }
          }
        ],
        [
          "OS == 'win'",
          {
            "defines": [
              "_CRT_SECURE_NO_WARNINGS"
            ]
          }
        ]
      ]
    }
  ]
}
