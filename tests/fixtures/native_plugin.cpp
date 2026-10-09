#include "bedrockforge.h"
#include <cstring>

namespace {
bf_result load(const bf_api* api) {
  if (!api || api->version != BF_API_VERSION) return BF_UNSUPPORTED;
  bf_item item{};
  std::strcpy(item.id, "framework_fixture:material");
  std::strcpy(item.name, "Fixture Material");
  std::strcpy(item.category, "loader-test");
  item.max_stack = 64;
  auto result = api->register_item(api->context, &item);
  if (result != BF_OK) return result;

  bf_recipe recipe{};
  std::strcpy(recipe.id, "framework_fixture:recipe");
  std::strcpy(recipe.provider, "framework_fixture");
  std::strcpy(recipe.output.item, "framework_fixture:material");
  recipe.output.count = 1;
  recipe.ingredient_count = 1;
  recipe.ingredients[0] = recipe.output;
  return api->register_recipe(api->context, &recipe);
}

void unload() {}
const bf_mod descriptor{sizeof(bf_mod), BF_API_VERSION, "framework_fixture", "0.1.0", nullptr, 0, load, unload};
}

extern "C" BF_EXPORT const bf_mod* bf_mod_entry(void) { return &descriptor; }
