cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(webhook_test)

# Generate hex files from pem files
set(gen_dir ${CMAKE_CURRENT_BINARY_DIR}/certs)
zephyr_include_directories(${gen_dir})

##DigiCert CA
# generate_inc_file_for_target(
#     app
#     cert/DigiCertGlobalG3.pem
#     ${gen_dir}/DigiCertGlobalG3.pem.inc
# )

# #Universal Global Root CA (too large for modem flash ~220KB)
# generate_inc_file_for_target(
#     app
#     cert/cacert.pem
#     ${gen_dir}/cacert.pem.inc
# )

# #Google GTS Root R1 (44KB)
# generate_inc_file_for_target(
#     app
#     cert/google_roots.pem
#     ${gen_dir}/google_roots.pem.inc
# )

#Leaf CA (5KB maybe small enough)
generate_inc_file_for_target(
    app
    cert/google_gts.pem
    ${gen_dir}/google_gts.pem.inc
)

target_sources(app PRIVATE
    src/main.c
    src/radar.c
    src/cellular.c
)
