#ifdef ROOD_PC
/* Old GCC lets these statics override an earlier extern (items1.h); modern
 * GCC rejects that, so give the file-local copies their own names. */
#define _secondaryTitleEnabled _toggleUi_secondaryTitleEnabled
#define _itemCategoryIconsEnabled _toggleUi_itemCategoryIconsEnabled
#define _statusCommandState _toggleUi_statusCommandState
#endif
static char _secondaryTitleEnabled;
static char _itemCategoryIconsEnabled;
static char _statusCommandState;

/**
 * Toggles the display of the secondary title, the category icons,
 * and the status command.
 */
static void _toggleMenuUi(char state)
{
    _secondaryTitleEnabled = state;
    _itemCategoryIconsEnabled = state;
    _statusCommandState = state;
}
