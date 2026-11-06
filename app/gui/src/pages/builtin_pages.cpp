#include "pages/builtin_pages.h"

#include "mcp/mcp_registration.h"
#include "pages/console_page.h"
#include "pages/flash_settings_pages.h"
#include "viewmodels/app_view_model.h"
#include "wizard/probe_picker_step.h"

namespace hwio::gui {

void registerBuiltInPages(AppViewModel& app) {
    registerConsolePage(app);           // plan 0005
    registerFlashAndSettingsPages(app); // plan 0006: Flash and the global Settings page
    (void)registerMcp(app);             // plan 0014 (ADR-0243): after the Flash services
    registerProbePickerStep(app);       // plan 0035 (ADR-0252): the wizards' probe step
}

} // namespace hwio::gui
