#include "text_view_model.h"

#include <asset_ids.h>

namespace bench {

TextViewModel::TextViewModel() {
    assets::ui::Text::bind(*this);
}

}
