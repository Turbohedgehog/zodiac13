# cmake -DSOURCE_DIR=<repo> -DOUTPUT_DIR=<dir> -P android_assets.cmake
# Copies config/ and assets/ into OUTPUT_DIR with z13_files.txt, the manifest
# android_main.cpp unpacks them by: a hash of every file, then one path per line.

set(directories config assets)
set(paths "")
set(hashes "")
foreach(directory IN LISTS directories)
    file(COPY "${SOURCE_DIR}/${directory}" DESTINATION "${OUTPUT_DIR}")
    file(GLOB_RECURSE files RELATIVE "${SOURCE_DIR}" "${SOURCE_DIR}/${directory}/*")
    list(SORT files)
    foreach(path IN LISTS files)
        file(SHA256 "${SOURCE_DIR}/${path}" hash)
        string(APPEND hashes "${hash} ${path}\n")
        string(APPEND paths "${path}\n")
    endforeach()
endforeach()

string(SHA256 manifest_hash "${hashes}")
file(WRITE "${OUTPUT_DIR}/z13_files.txt" "${manifest_hash}\n${paths}")
