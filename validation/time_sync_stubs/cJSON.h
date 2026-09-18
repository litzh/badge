#pragma once
#include <map>
#include <string>
#include <vector>
struct cJSON {
  std::map<std::string, std::string> fields;
  std::vector<cJSON *> children;
  ~cJSON() { for (auto *child : children) delete child; }
};
inline cJSON *cJSON_CreateObject() { return new cJSON; }
inline void cJSON_AddStringToObject(cJSON *j, const char *key, const char *value) {
  j->fields[key] = value;
}
inline void cJSON_AddNullToObject(cJSON *j, const char *key) { j->fields[key] = "null"; }
inline void cJSON_AddBoolToObject(cJSON *j, const char *key, bool value) {
  j->fields[key] = value ? "true" : "false";
}
inline void cJSON_AddNumberToObject(cJSON *j, const char *key, double n) {
  j->fields[key] = std::to_string(n);
}
inline cJSON *cJSON_AddArrayToObject(cJSON *j, const char *) {
  auto *a = new cJSON;
  j->children.push_back(a);
  return a;
}
inline cJSON *cJSON_CreateString(const char *value) {
  auto *j = new cJSON;
  j->fields["value"] = value;
  return j;
}
inline void cJSON_AddItemToArray(cJSON *j, cJSON *child) { j->children.push_back(child); }
