#include "rule_line_view_model.h"

#include <asset_ids.h>

namespace editor {

RuleLineViewModel::RuleLineViewModel() {
    assets::ui::Inspector::Rules::bind(*this);
}

}
