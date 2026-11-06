// The groups of checks run by fh_tests (main.cpp). Groups that compare with the JavaScript
// version's recorded answers take the folder that holds the .golden files.
#pragma once

#include <string>

void expr_tests(const std::string& parity_dir);
void preset_tests(const std::string& parity_dir);
void analyser_tests();
void image_tests();
void userfiles_tests();
void unicode_tests();
void findroot_tests();
void scene_tests();
void hearing_tests(const std::string& parity_dir);
void runtime_tests(const std::string& parity_dir);
void fractal_tests(const std::string& parity_dir);
void milk_tests(const std::string& parity_dir);
void state_tests(const std::string& parity_dir);
