if ((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  if(CONFIG_ZMK_TEXT_EXPANDER)
    zephyr_library_sources(
      src/text_expander.c
      src/trie.c
      src/hid_utils.c
      src/expansion_engine.c
    )
    zephyr_library_include_directories(include)
  endif()
endif()
