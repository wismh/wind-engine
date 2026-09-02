#include "cell_view_model.h"

#include <engine/ui/binding_id.h>

namespace bench {

CellViewModel::CellViewModel() {
    paint(engine::ui::intern("arc"), arc);
}

}
