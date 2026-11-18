add_cfe_app(ids_app fsw/src/ids_app.c fsw/src/ids_detector.c fsw/src/ids_features.c)
target_include_directories(ids_app PRIVATE ${APPLICATION_PLATFORM_INC_LIST})
target_link_libraries(ids_app m)
