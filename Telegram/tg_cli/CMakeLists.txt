add_executable(tg_cli)
init_non_host_target(tg_cli)

target_sources(tg_cli PRIVATE
    main.cpp
    capabilities/session_service_capabilities_cli.cpp
    capabilities/storage_settings_capabilities_cli.cpp
)

target_link_libraries(tg_cli
PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
)

set_target_properties(tg_cli PROPERTIES OUTPUT_NAME tg_cli)