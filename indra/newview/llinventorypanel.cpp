/*
 * @file llinventorypanel.cpp
 * @brief Implementation of the inventory panel and associated stuff.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"
#include "llinventorypanel.h"
#include "llwearabletype.h"

#include <algorithm>
#include <utility> // for std::pair<>

#include "llagent.h"
#include "llagentwearables.h"
#include "llappearancemgr.h"
#include "llavataractions.h"
#include "llavatarnamecache.h"
#include "llavatarpropertiesprocessor.h"
#include "llclipboard.h"
#include "lldate.h"
#include "llfloaterreg.h"
#include "llfloatersidepanelcontainer.h"
#include "llfolderview.h"
#include "llfolderviewitem.h"
#include "llfloaterimcontainer.h"
#include "llimview.h"
#include "llinspecttexture.h"
#include "llinventorybridge.h"
#include "llinventoryfunctions.h"
#include "llinventorymodelbackgroundfetch.h"
#include "llmenugl.h"
#include "llnotificationsutil.h"
#include "llpanelmaininventory.h"
#include "llpreview.h"
#include "llsidepanelinventory.h"
#include "llstartup.h"
#include "lltrans.h"
#include "llviewerassettype.h"
#include "llviewerattachmenu.h"
#include "llviewerfoldertype.h"
#include "llvoavatarself.h"

class LLInventoryFavoritesItemsPanel;
class LLInventoryCreatorItemsPanel;
class LLInventoryRecentItemsPanel;
class LLAssetFilteredInventoryPanel;

static LLDefaultChildRegistry::Register<LLInventoryPanel> r("inventory_panel");
static LLDefaultChildRegistry::Register<LLInventoryRecentItemsPanel> t_recent_inventory_panel("recent_inventory_panel");
static LLDefaultChildRegistry::Register<LLInventoryFavoritesItemsPanel> t_favorites_inventory_panel("favorites_inventory_panel");
static LLDefaultChildRegistry::Register<LLInventoryCreatorItemsPanel> t_creator_inventory_panel("creator_inventory_panel");
static LLDefaultChildRegistry::Register<LLAssetFilteredInventoryPanel> t_asset_filtered_inv_panel("asset_filtered_inv_panel");

const std::string LLInventoryPanel::DEFAULT_SORT_ORDER = std::string("InventorySortOrder");
const std::string LLInventoryPanel::RECENTITEMS_SORT_ORDER = std::string("RecentItemsSortOrder");
const std::string LLInventoryPanel::INHERIT_SORT_ORDER = std::string("");
static const LLInventoryFolderViewModelBuilder INVENTORY_BRIDGE_BUILDER;

// statics
bool LLInventoryPanel::sColorSetInitialized = false;
LLUIColor LLInventoryPanel::sDefaultColor;
LLUIColor LLInventoryPanel::sDefaultHighlightColor;
LLUIColor LLInventoryPanel::sLibraryColor;
LLUIColor LLInventoryPanel::sLinkColor;

const LLColor4U DEFAULT_WHITE(255, 255, 255);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Class LLInventoryPanelObserver
//
// Bridge to support knowing when the inventory has changed.
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class LLInventoryPanelObserver : public LLInventoryObserver
{
public:
    LLInventoryPanelObserver(LLInventoryPanel* ip) : mIP(ip) {}
    virtual ~LLInventoryPanelObserver() {}
    virtual void changed(U32 mask)
    {
        mIP->modelChanged(mask);
    }
protected:
    LLInventoryPanel* mIP;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Class LLInvPanelComplObserver
//
// Calls specified callback when all specified items become complete.
//
// Usage:
// observer = new LLInvPanelComplObserver(boost::bind(onComplete));
// inventory->addObserver(observer);
// observer->reset(); // (optional)
// observer->watchItem(incomplete_item1_id);
// observer->watchItem(incomplete_item2_id);
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class LLInvPanelComplObserver : public LLInventoryCompletionObserver
{
public:
    typedef std::function<void()> callback_t;

    LLInvPanelComplObserver(callback_t cb)
    :   mCallback(cb)
    {
    }

    void reset();

private:
    /*virtual*/ void done();

    /// Called when all the items are complete.
    callback_t  mCallback;
};

void LLInvPanelComplObserver::reset()
{
    mIncomplete.clear();
    mComplete.clear();
}

void LLInvPanelComplObserver::done()
{
    mCallback();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Class LLInventoryPanel
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

LLInventoryPanel::LLInventoryPanel(const LLInventoryPanel::Params& p) :
    LLPanel(p),
    mInventoryObserver(NULL),
    mCompletionObserver(NULL),
    mScroller(NULL),
    mSortOrderSetting(p.sort_order_setting),
    mInventory(p.inventory), //inventory("", &gInventory)
    mAcceptsDragAndDrop(p.accepts_drag_and_drop),
    mAllowMultiSelect(p.allow_multi_select),
    mAllowDrag(p.allow_drag),
    mShowItemLinkOverlays(p.show_item_link_overlays),
    mShowEmptyMessage(p.show_empty_message),
    mSuppressFolderMenu(p.suppress_folder_menu),
    mSuppressOpenItemAction(false),
    mBuildViewsOnInit(p.preinitialize_views),
    mViewsInitialized(VIEWS_UNINITIALIZED),
    mInvFVBridgeBuilder(NULL),
    mInventoryViewModel(p.name),
    mGroupedItemBridge(new LLFolderViewGroupedItemBridge),
    mFocusSelection(false),
    mBuildChildrenViews(true),
    mRootInited(false)
{
    mInvFVBridgeBuilder = &INVENTORY_BRIDGE_BUILDER;

    if (!sColorSetInitialized)
    {
        sDefaultColor = LLUIColorTable::instance().getColor("InventoryItemColor", DEFAULT_WHITE);
        sDefaultHighlightColor = LLUIColorTable::instance().getColor("MenuItemHighlightFgColor", DEFAULT_WHITE);
        sLibraryColor = LLUIColorTable::instance().getColor("InventoryItemLibraryColor", DEFAULT_WHITE);
        sLinkColor = LLUIColorTable::instance().getColor("InventoryItemLinkColor", DEFAULT_WHITE);
        sColorSetInitialized = true;
    }

    // context menu callbacks
    mCommitCallbackRegistrar.add("Inventory.DoToSelected", boost::bind(&LLInventoryPanel::doToSelected, this, _2));
    mCommitCallbackRegistrar.add("Inventory.EmptyTrash", boost::bind(&LLInventoryModel::emptyFolderType, &gInventory, "ConfirmEmptyTrash", LLFolderType::FT_TRASH));
    mCommitCallbackRegistrar.add("Inventory.EmptyLostAndFound", boost::bind(&LLInventoryModel::emptyFolderType, &gInventory, "ConfirmEmptyLostAndFound", LLFolderType::FT_LOST_AND_FOUND));
    mCommitCallbackRegistrar.add("Inventory.DoCreate", boost::bind(&LLInventoryPanel::doCreate, this, _2));
    mCommitCallbackRegistrar.add("Inventory.AttachObject", boost::bind(&LLInventoryPanel::attachObject, this, _2));
    mCommitCallbackRegistrar.add("Inventory.BeginIMSession", boost::bind(&LLInventoryPanel::beginIMSession, this));
    mCommitCallbackRegistrar.add("Inventory.Share",  boost::bind(&LLAvatarActions::shareWithAvatars, this));
    mCommitCallbackRegistrar.add("Inventory.FileUploadLocation", boost::bind(&LLInventoryPanel::fileUploadLocation, this, _2));
    mEnableCallbackRegistrar.add("Inventory.FileUploadLocation.Check", boost::bind(&LLInventoryPanel::isUploadLocationSelected, this, _2));
    mCommitCallbackRegistrar.add("Inventory.OpenNewFolderWindow", boost::bind(&LLInventoryPanel::openSingleViewInventory, this, LLUUID()));
}

LLFolderView * LLInventoryPanel::createFolderRoot(LLUUID root_id )
{
    LLFolderView::Params p(mParams.folder_view);
    p.name = getName();
    p.title = getLabel();
    p.rect = LLRect(0, 0, getRect().getWidth(), 0);
    p.parent_panel = this;
    p.listener = mInvFVBridgeBuilder->createBridge( LLAssetType::AT_CATEGORY,
                                                                    LLAssetType::AT_CATEGORY,
                                                                    LLInventoryType::IT_CATEGORY,
                                                                    this,
                                                                    &mInventoryViewModel,
                                                                    NULL,
                                                                    root_id);
    p.view_model = &mInventoryViewModel;
    p.grouped_item_model = mGroupedItemBridge;
    p.use_label_suffix = mParams.use_label_suffix;
    p.allow_multiselect = mAllowMultiSelect;
    p.allow_drag = mAllowDrag;
    p.show_empty_message = mShowEmptyMessage;
    p.suppress_folder_menu = mSuppressFolderMenu;
    p.show_item_link_overlays = mShowItemLinkOverlays;
    p.root = NULL;
    p.allow_drop = mParams.allow_drop_on_root;
    p.options_menu = "menu_inventory.xml";

    LLFolderView* fv = LLUICtrlFactory::create<LLFolderView>(p);
    fv->setCallbackRegistrar(&mCommitCallbackRegistrar);
    fv->setEnableRegistrar(&mEnableCallbackRegistrar);

    return fv;
}

void LLInventoryPanel::clearFolderRoot()
{
    gIdleCallbacks.deleteFunction(idle, this);
    gIdleCallbacks.deleteFunction(onIdle, this);

    if (mInventoryObserver)
    {
        mInventory->removeObserver(mInventoryObserver);
        delete mInventoryObserver;
        mInventoryObserver = NULL;
    }
    if (mCompletionObserver)
    {
        mInventory->removeObserver(mCompletionObserver);
        delete mCompletionObserver;
        mCompletionObserver = NULL;
    }

    if (mScroller)
    {
        removeChild(mScroller);
        delete mScroller;
        mScroller = NULL;
    }
}

void LLInventoryPanel::initFromParams(const LLInventoryPanel::Params& params)
{
    // save off copy of params
    mParams = params;

    initFolderRoot();

    // Initialize base class params.
    LLPanel::initFromParams(mParams);
}

LLInventoryPanel::~LLInventoryPanel()
{
    U32 sort_order = getFolderViewModel()->getSorter().getSortOrder();
    if (mSortOrderSetting != INHERIT_SORT_ORDER)
    {
        gSavedSettings.setU32(mSortOrderSetting, sort_order);
    }

    clearFolderRoot();
}

void LLInventoryPanel::initFolderRoot()
{
    // Clear up the root view
    // Note: This needs to be done *before* we build the new folder view
    LLUUID root_id = getRootFolderID();
    if (mFolderRoot.get())
    {
        removeItemID(root_id);
        mFolderRoot.get()->destroyView();
    }

    mCommitCallbackRegistrar.pushScope(); // registered as a widget; need to push callback scope ourselves
    {
        // Determine the root folder in case specified, and
        // build the views starting with that folder.
        LLFolderView* folder_view = createFolderRoot(root_id);
        mFolderRoot = folder_view->getHandle();
        mRootInited = true;

        addItemID(root_id, mFolderRoot.get());
    }
    mCommitCallbackRegistrar.popScope();
    mFolderRoot.get()->setCallbackRegistrar(&mCommitCallbackRegistrar);
    mFolderRoot.get()->setEnableRegistrar(&mEnableCallbackRegistrar);

    // Scroller
    LLRect scroller_view_rect = getRect();
    scroller_view_rect.translate(-scroller_view_rect.mLeft, -scroller_view_rect.mBottom);
    LLScrollContainer::Params scroller_params(mParams.scroll());
    scroller_params.rect(scroller_view_rect);
    mScroller = LLUICtrlFactory::create<LLFolderViewScrollContainer>(scroller_params);
    addChild(mScroller);
    mScroller->addChild(mFolderRoot.get());
    mFolderRoot.get()->setScrollContainer(mScroller);
    mFolderRoot.get()->setFollowsAll();
    mFolderRoot.get()->addChild(mFolderRoot.get()->mStatusTextBox);

    if (mSelectionCallback)
    {
        mFolderRoot.get()->setSelectCallback(mSelectionCallback);
    }

    // Set up the callbacks from the inventory we're viewing, and then build everything.
    mInventoryObserver = new LLInventoryPanelObserver(this);
    mInventory->addObserver(mInventoryObserver);

    mCompletionObserver = new LLInvPanelComplObserver(boost::bind(&LLInventoryPanel::onItemsCompletion, this));
    mInventory->addObserver(mCompletionObserver);

    if (mBuildViewsOnInit)
    {
        initializeViewBuilding();
    }

    if (mSortOrderSetting != INHERIT_SORT_ORDER)
    {
        setSortOrder(gSavedSettings.getU32(mSortOrderSetting));
    }
    else
    {
        setSortOrder(gSavedSettings.getU32(DEFAULT_SORT_ORDER));
    }

    // hide inbox
    if (!gSavedSettings.getBOOL("InventoryOutboxMakeVisible"))
    {
        getFilter().setFilterCategoryTypes(getFilter().getFilterCategoryTypes() & ~(1ULL << LLFolderType::FT_INBOX));
    }
    // hide marketplace listing box, unless we are a marketplace panel
    if (!gSavedSettings.getBOOL("InventoryOutboxMakeVisible") && !mParams.use_marketplace_folders)
    {
        getFilter().setFilterCategoryTypes(getFilter().getFilterCategoryTypes() & ~(1ULL << LLFolderType::FT_MARKETPLACE_LISTINGS));
    }

    // set the filter for the empty folder if the debug setting is on
    if (gSavedSettings.getBOOL("DebugHideEmptySystemFolders"))
    {
        getFilter().setFilterEmptySystemFolders();
    }

    // keep track of the clipboard state so that we avoid filtering too much
    mClipboardState = LLClipboard::instance().getGeneration();
}

void LLInventoryPanel::initializeViewBuilding()
{
    if (mViewsInitialized == VIEWS_UNINITIALIZED)
    {
        LL_DEBUGS("Inventory") << "Setting views for " << getName() << " to initialize" << LL_ENDL;
        // Build view of inventory if we need default full hierarchy and inventory is ready, otherwise do in onIdle.
        // Initializing views takes a while so always do it onIdle if viewer already loaded.
        if (mInventory->isInventoryUsable()
            && LLStartUp::getStartupState() <= STATE_WEARABLES_WAIT)
        {
            LLTimer timer;
            // Usually this happens on login, so we have less time constraits, but too long and we can cause a disconnect
            const F64 max_time = 20.f;
            initializeViews(max_time);

            if (mViewsInitialized == VIEWS_INITIALIZED)
            {
                LL_INFOS("Inventory")
                    << "Fully initialized inventory panel " << getName()
                    << " with " << (S32)mItemMap.size()
                    << " views in " << timer.getElapsedTimeF32() << " seconds."
                    << LL_ENDL;
            }
            else
            {
                LL_INFOS("Inventory")
                    << "Partially initialized inventory panel " << getName()
                    << " with " << (S32)mItemMap.size()
                    << " views in " << timer.getElapsedTimeF32()
                    << " seconds. Pending known views: " << (S32)mBuildViewsQueue.size()
                    << LL_ENDL;
            }
        }
        else
        {
            mViewsInitialized = VIEWS_INITIALIZING;
            gIdleCallbacks.addFunction(onIdle, (void*)this);
        }
    }
}

/*virtual*/
void LLInventoryPanel::onVisibilityChange(bool new_visibility)
{
    if (new_visibility && mViewsInitialized == VIEWS_UNINITIALIZED)
    {
        // first call can be from tab initialization
        if (gFloaterView->getParentFloater(this) != NULL)
        {
            initializeViewBuilding();
        }
    }
    LLPanel::onVisibilityChange(new_visibility);
}

void LLInventoryPanel::draw()
{
    // Select the desired item (in case it wasn't loaded when the selection was requested)
    updateSelection();

    LLPanel::draw();
}

const LLInventoryFilter& LLInventoryPanel::getFilter() const
{
    return getFolderViewModel()->getFilter();
}

LLInventoryFilter& LLInventoryPanel::getFilter()
{
    return getFolderViewModel()->getFilter();
}

void LLInventoryPanel::setFilterTypes(U64 types, LLInventoryFilter::EFilterType filter_type)
{
    if (filter_type == LLInventoryFilter::FILTERTYPE_OBJECT)
    {
        getFilter().setFilterObjectTypes(types);
    }
    else if (filter_type == LLInventoryFilter::FILTERTYPE_CATEGORY)
    {
        getFilter().setFilterCategoryTypes(types);
    }
}

void LLInventoryPanel::setFilterWorn()
{
    getFilter().setFilterWorn();
}

U32 LLInventoryPanel::getFilterObjectTypes() const
{
    return (U32)getFilter().getFilterObjectTypes();
}

U32 LLInventoryPanel::getFilterPermMask() const
{
    return getFilter().getFilterPermissions();
}


void LLInventoryPanel::setFilterPermMask(PermissionMask filter_perm_mask)
{
    getFilter().setFilterPermissions(filter_perm_mask);
}

void LLInventoryPanel::setFilterWearableTypes(U64 types)
{
    getFilter().setFilterWearableTypes(types);
}

void LLInventoryPanel::setFilterSettingsTypes(U64 filter)
{
    getFilter().setFilterSettingsTypes(filter);
}

void LLInventoryPanel::setFilterSubString(const std::string& string)
{
    getFilter().setFilterSubString(string);
}

const std::string LLInventoryPanel::getFilterSubString()
{
    return getFilter().getFilterSubString();
}

void LLInventoryPanel::setSortOrder(U32 order)
{
    LLInventorySort sorter(order);
    if (order != getFolderViewModel()->getSorter().getSortOrder())
    {
        getFolderViewModel()->setSorter(sorter);
        mFolderRoot.get()->arrangeAll();
        // try to keep selection onscreen, even if it wasn't to start with
        mFolderRoot.get()->scrollToShowSelection();
    }
}

U32 LLInventoryPanel::getSortOrder() const
{
    return getFolderViewModel()->getSorter().getSortOrder();
}

void LLInventoryPanel::setSinceLogoff(bool sl)
{
    getFilter().setDateRangeLastLogoff(sl);
}

void LLInventoryPanel::setHoursAgo(U32 hours)
{
    getFilter().setHoursAgo(hours);
}

void LLInventoryPanel::setDateSearchDirection(U32 direction)
{
    getFilter().setDateSearchDirection(direction);
}

void LLInventoryPanel::setFilterLinks(U64 filter_links)
{
    getFilter().setFilterLinks(filter_links);
}

void LLInventoryPanel::setSearchType(LLInventoryFilter::ESearchType type)
{
    getFilter().setSearchType(type);
}

LLInventoryFilter::ESearchType LLInventoryPanel::getSearchType()
{
    return getFilter().getSearchType();
}

void LLInventoryPanel::setShowFolderState(LLInventoryFilter::EFolderShow show)
{
    getFilter().setShowFolderState(show);
}

LLInventoryFilter::EFolderShow LLInventoryPanel::getShowFolderState()
{
    return getFilter().getShowFolderState();
}

void LLInventoryPanel::itemChanged(const LLUUID& item_id, U32 mask, const LLInventoryObject* model_item)
{
    LLFolderViewItem* view_item = getItemByID(item_id);
    LLFolderViewModelItemInventory* viewmodel_item =
        static_cast<LLFolderViewModelItemInventory*>(view_item ? view_item->getViewModelItem() : NULL);

    // LLFolderViewFolder is derived from LLFolderViewItem so dynamic_cast from item
    // to folder is the fast way to get a folder without searching through folders tree.
    LLFolderViewFolder* view_folder = NULL;

    // Check requires as this item might have already been deleted
    // as a child of its deleted parent.
    if (model_item && view_item)
    {
        view_folder = dynamic_cast<LLFolderViewFolder*>(view_item);
    }

    // if folder is not fully initialized (likely due to delayed load on idle)
    // and we are not rebuilding, try updating children
    if (view_folder
        && !view_folder->areChildrenInited()
        && ( (mask & LLInventoryObserver::REBUILD) == 0))
    {
        LLInventoryObject const* objectp = mInventory->getObject(item_id);
        if (objectp)
        {
            view_item = buildNewViews(item_id, objectp, view_item, BUILD_ONE_FOLDER);
        }
    }

    //////////////////////////////
    // LABEL Operation
    // Empty out the display name for relabel.
    if (mask & LLInventoryObserver::LABEL)
    {
        if (view_item)
        {
            // Request refresh on this item (also flags for filtering)
            LLInvFVBridge* bridge = (LLInvFVBridge*)view_item->getViewModelItem();
            if(bridge)
            {
                // Clear the searchable name first, so it gets
                // properly re-built during refresh()
                bridge->clearSearchableName();

                view_item->refresh();
            }
            LLFolderViewFolder* parent = view_item->getParentFolder();
            if(parent && parent->getViewModelItem())
            {
                parent->getViewModelItem()->dirtyDescendantsFilter();
            }
        }
    }

    //////////////////////////////
    // REBUILD Operation
    // Destroy and regenerate the UI.
    if (mask & LLInventoryObserver::REBUILD)
    {
        if (model_item && view_item && viewmodel_item)
        {
            const LLUUID idp = viewmodel_item->getUUID();
            view_item->destroyView();
            removeItemID(idp);
        }

        LLInventoryObject const* objectp = mInventory->getObject(item_id);
        if (objectp)
        {
            // providing NULL directly avoids unnessesary getItemByID calls
            view_item = buildNewViews(item_id, objectp, NULL, BUILD_ONE_FOLDER);
        }
        else
        {
            view_item = NULL;
        }

        viewmodel_item =
            static_cast<LLFolderViewModelItemInventory*>(view_item ? view_item->getViewModelItem() : NULL);
        view_folder = dynamic_cast<LLFolderViewFolder *>(view_item);
    }

    //////////////////////////////
    // INTERNAL Operation
    // This could be anything.  For now, just refresh the item.
    if (mask & LLInventoryObserver::INTERNAL)
    {
        if (view_item && view_item->getViewModelItem())
        {
            view_item->refresh();
        }
    }

    //////////////////////////////
    // SORT Operation
    // Sort the folder.
    if (mask & LLInventoryObserver::SORT)
    {
        if (view_folder && view_folder->getViewModelItem())
        {
            view_folder->getViewModelItem()->requestSort();
        }
    }

    if (mask & LLInventoryObserver::UPDATE_FAVORITE)
    {
        if (view_item && view_item->getViewModelItem())
        {
            view_item->refresh();
            LLFolderViewFolder* parent = view_item->getParentFolder();
            if (parent)
            {
                parent->updateHasFavorites(get_is_favorite(model_item));
            }
        }
    }

    // We don't typically care which of these masks the item is actually flagged with, since the masks
    // may not be accurate (e.g. in the main inventory panel, I move an item from My Inventory into
    // Landmarks; this is a STRUCTURE change for that panel but is an ADD change for the Landmarks
    // panel).  What's relevant is that the item and UI are probably out of sync and thus need to be
    // resynchronized.
    if (mask & (LLInventoryObserver::STRUCTURE |
                LLInventoryObserver::ADD |
                LLInventoryObserver::REMOVE))
    {
        //////////////////////////////
        // ADD Operation
        // Item exists in memory but a UI element hasn't been created for it.
        if (model_item && !view_item)
        {
            // Add the UI element for this item.
            LLInventoryObject const* objectp = mInventory->getObject(item_id);
            if (objectp)
            {
                // providing NULL directly avoids unnessesary getItemByID calls
                buildNewViews(item_id, objectp, NULL, BUILD_ONE_FOLDER);
            }

            // Select any newly created object that has the auto rename at top of folder root set.
            if(mFolderRoot.get() && mFolderRoot.get()->getRoot()->needsAutoRename())
            {
                setSelection(item_id, false);
            }
            updateFolderLabel(model_item->getParentUUID());

            if (get_is_favorite(model_item))
            {
                LLFolderViewFolder* new_parent = getFolderByID(model_item->getParentUUID());
                if (new_parent)
                {
                    new_parent->updateHasFavorites(true);
                }
            }

        }

        //////////////////////////////
        // STRUCTURE Operation
        // This item already exists in both memory and UI.  It was probably reparented.
        else if (model_item && view_item)
        {
            LLFolderViewFolder* old_parent = view_item->getParentFolder();
            // Don't process the item if it is the root
            if (old_parent)
            {
                LLFolderViewModelItem* old_parent_vmi = old_parent->getViewModelItem();
                LLFolderViewModelItemInventory* viewmodel_folder = static_cast<LLFolderViewModelItemInventory*>(old_parent_vmi);
                LLFolderViewFolder* new_parent = getFolderByID(model_item->getParentUUID());

                if (old_parent != new_parent // Item has been moved.
                    && (new_parent != NULL || !isInRootContent(item_id, view_item)) // item is not or shouldn't be in root content
                    )
                {
                    if (new_parent != NULL)
                    {
                        // Item is to be moved and we found its new parent in the panel's directory, so move the item's UI.
                        view_item->addToFolder(new_parent);
                        addItemID(viewmodel_item->getUUID(), view_item);
                        if (mInventory)
                        {
                            const LLUUID trash_id = mInventory->findCategoryUUIDForType(LLFolderType::FT_TRASH);
                            if (trash_id != model_item->getParentUUID() && (mask & LLInventoryObserver::INTERNAL) && new_parent->isOpen())
                            {
                                setSelection(item_id, false);
                            }
                        }
                        updateFolderLabel(model_item->getParentUUID());
                    }
                    else
                    {
                        // Remove the item ID before destroying the view because the view-model-item gets
                        // destroyed when the view is destroyed
                        removeItemID(viewmodel_item->getUUID());

                        // Item is to be moved outside the panel's directory (e.g. moved to trash for a panel that
                        // doesn't include trash).  Just remove the item's UI.
                        view_item->destroyView();
                    }
                    if(viewmodel_folder)
                    {
                        updateFolderLabel(viewmodel_folder->getUUID());
                    }
                    if (old_parent_vmi)
                    {
                        old_parent_vmi->dirtyDescendantsFilter();
                    }

                    if (view_item->isFavorite())
                    {
                        if (old_parent)
                        {
                        old_parent->updateHasFavorites(false); // favorite was removed
                        }
                        if (new_parent)
                        {
                        new_parent->updateHasFavorites(true); // favorite was added
                    }
                }
            }
        }
        }

        //////////////////////////////
        // REMOVE Operation
        // This item has been removed from memory, but its associated UI element still exists.
        else if (!model_item && view_item && viewmodel_item)
        {
            // Remove the item's UI.
            LLFolderViewFolder* parent = view_item->getParentFolder();
            removeItemID(viewmodel_item->getUUID());
            bool was_favorite = view_item->isFavorite();
            view_item->destroyView();
            if(parent)
            {
                LLFolderViewModelItem* parent_wmi = parent->getViewModelItem();
                if (parent_wmi)
                {
                    parent_wmi->dirtyDescendantsFilter();
                    LLFolderViewModelItemInventory* viewmodel_folder = static_cast<LLFolderViewModelItemInventory*>(parent_wmi);
                    if (viewmodel_folder)
                    {
                        updateFolderLabel(viewmodel_folder->getUUID());
                    }
                }
                if (was_favorite)
                {
                    parent->updateHasFavorites(false); // favorite was removed
                }
            }
        }
    }
}

// Called when something changed in the global model (new item, item coming through the wire, rename, move, etc...) (CHUI-849)
void LLInventoryPanel::modelChanged(U32 mask)
{
    LL_PROFILE_ZONE_SCOPED;

    if (mViewsInitialized != VIEWS_INITIALIZED) return; // todo: Store changes if building?

    const LLInventoryModel* model = getModel();
    if (!model) return;

    const LLInventoryModel::changed_items_t& changed_items = model->getChangedIDs();
    if (changed_items.empty()) return;

    for (LLInventoryModel::changed_items_t::const_iterator items_iter = changed_items.begin();
         items_iter != changed_items.end();
         ++items_iter)
    {
        const LLUUID& item_id = (*items_iter);
        const LLInventoryObject* model_item = model->getObject(item_id);
        itemChanged(item_id, mask, model_item);
    }
}

LLUUID LLInventoryPanel::getRootFolderID()
{
    LLUUID root_id;
    if (mFolderRoot.get() && mFolderRoot.get()->getViewModelItem())
    {
        root_id = static_cast<LLFolderViewModelItemInventory*>(mFolderRoot.get()->getViewModelItem())->getUUID();
    }
    else
    {
        if (mParams.start_folder.id.isChosen())
        {
            root_id = mParams.start_folder.id;
        }
        else
        {
            const LLFolderType::EType preferred_type = mParams.start_folder.type.isChosen()
                ? mParams.start_folder.type
                : LLViewerFolderType::lookupTypeFromNewCategoryName(mParams.start_folder.name);

            if ("LIBRARY" == mParams.start_folder.name())
            {
                root_id = gInventory.getLibraryRootFolderID();
            }
            else if (preferred_type != LLFolderType::FT_NONE)
            {
                LLStringExplicit label(mParams.start_folder.name());
                setLabel(label);

                root_id = gInventory.findCategoryUUIDForType(preferred_type);
                if (root_id.isNull())
                {
                    LL_WARNS() << "Could not find folder of type " << preferred_type << LL_ENDL;
                    root_id.generateNewID();
                }
            }
        }
    }
    return root_id;
}

// static
void LLInventoryPanel::onIdle(void *userdata)
{
    if (!gInventory.isInventoryUsable())
        return;

    LLInventoryPanel *self = (LLInventoryPanel*)userdata;
    if (self->mViewsInitialized <= VIEWS_INITIALIZING)
    {
        const F64 max_time = 0.001f; // 1 ms, in this case we need only root folders
        self->initializeViews(max_time); // Shedules LLInventoryPanel::idle()
    }
    if (self->mViewsInitialized >= VIEWS_BUILDING)
    {
        gIdleCallbacks.deleteFunction(onIdle, (void*)self);
    }
}

struct DirtyFilterFunctor : public LLFolderViewFunctor
{
    /*virtual*/ void doFolder(LLFolderViewFolder* folder)
    {
        folder->getViewModelItem()->dirtyFilter();
    }
    /*virtual*/ void doItem(LLFolderViewItem* item)
    {
        item->getViewModelItem()->dirtyFilter();
    }
};

void LLInventoryPanel::idle(void* user_data)
{
    LLInventoryPanel* panel = (LLInventoryPanel*)user_data;
    // Nudge the filter if the clipboard state changed
    if (panel->mClipboardState != LLClipboard::instance().getGeneration())
    {
        panel->mClipboardState = LLClipboard::instance().getGeneration();
        const LLUUID trash_id = gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH);
        LLFolderViewFolder* trash_folder = panel->getFolderByID(trash_id);
        if (trash_folder)
        {
            DirtyFilterFunctor dirtyFilterFunctor;
            trash_folder->applyFunctorToChildren(dirtyFilterFunctor);
        }

    }

    bool in_visible_chain = panel->isInVisibleChain();

    if (!panel->mBuildRootQueue.empty())
    {
        const F64 max_time = in_visible_chain ? 0.006f : 0.001f; // 6 ms
        F64 curent_time = LLTimer::getTotalSeconds();
        panel->mBuildViewsEndTime = curent_time + max_time;

        while (curent_time < panel->mBuildViewsEndTime
            && !panel->mBuildRootQueue.empty())
        {
            LLUUID item_id = panel->mBuildRootQueue.back();
            panel->mBuildRootQueue.pop_back();
            panel->findAndInitRootContent(item_id);

            curent_time = LLTimer::getTotalSeconds();
        }
    }
    else if (!panel->mBuildViewsQueue.empty())
    {
        const F64 max_time = in_visible_chain ? 0.006f : 0.001f; // 6 ms
        F64 curent_time = LLTimer::getTotalSeconds();
        panel->mBuildViewsEndTime = curent_time + max_time;

        // things added last are closer to root thus of higher priority
        std::deque<LLUUID> priority_list;
        priority_list.swap(panel->mBuildViewsQueue);

        while (curent_time < panel->mBuildViewsEndTime
            && !priority_list.empty())
        {
            LLUUID item_id = priority_list.back();
            priority_list.pop_back();

            LLInventoryObject const* objectp = panel->mInventory->getObject(item_id);
            if (objectp && panel->typedViewsFilter(item_id, objectp))
            {
                LLFolderViewItem* folder_view_item = panel->getItemByID(item_id);
                if (!folder_view_item || !folder_view_item->areChildrenInited())
                {
                    const LLUUID &parent_id = objectp->getParentUUID();
                    LLFolderViewFolder* parent_folder = (LLFolderViewFolder*)panel->getItemByID(parent_id);
                    panel->buildViewsTree(item_id, parent_id, objectp, folder_view_item, parent_folder, BUILD_TIMELIMIT);
                }
            }
            curent_time = LLTimer::getTotalSeconds();
        }
        while (!priority_list.empty())
        {
            // items in priority_list are of higher priority
            panel->mBuildViewsQueue.push_back(priority_list.front());
            priority_list.pop_front();
        }
        if (panel->mBuildViewsQueue.empty())
        {
            panel->mViewsInitialized = VIEWS_INITIALIZED;
        }
    }
    // in case panel is empty or only has 'roots'
    else if (panel->mViewsInitialized == VIEWS_BUILDING)
    {
        panel->mViewsInitialized = VIEWS_INITIALIZED;
    }

    // Take into account the fact that the root folder might be invalidated
    if (panel->mFolderRoot.get())
    {
        panel->mFolderRoot.get()->update();
        // while dragging, update selection rendering to reflect single/multi drag status
        if (LLToolDragAndDrop::getInstance()->hasMouseCapture())
        {
            EAcceptance last_accept = LLToolDragAndDrop::getInstance()->getLastAccept();
            if (last_accept == ACCEPT_YES_SINGLE || last_accept == ACCEPT_YES_COPY_SINGLE)
            {
                panel->mFolderRoot.get()->setShowSingleSelection(true);
            }
            else
            {
                panel->mFolderRoot.get()->setShowSingleSelection(false);
            }
        }
        else
        {
            panel->mFolderRoot.get()->setShowSingleSelection(false);
        }
    }
    else
    {
        LL_WARNS() << "Inventory : Deleted folder root detected on panel" << LL_ENDL;
        panel->clearFolderRoot();
    }
}


void LLInventoryPanel::initializeViews(F64 max_time)
{
    if (!gInventory.isInventoryUsable()) return;
    if (!mRootInited) return;

    mViewsInitialized = VIEWS_BUILDING;

    F64 curent_time = LLTimer::getTotalSeconds();
    mBuildViewsEndTime = curent_time + max_time;

    // init everything
    initRootContent();

    if (mBuildViewsQueue.empty() && mBuildRootQueue.empty())
    {
        mViewsInitialized = VIEWS_INITIALIZED;
    }

    gIdleCallbacks.addFunction(idle, this);

    if(mParams.open_first_folder)
    {
        openStartFolderOrMyInventory();
    }

    // Special case for new user login
    if (gAgent.isFirstLogin())
    {
        // Auto open the user's library
        LLFolderViewFolder* lib_folder =   getFolderByID(gInventory.getLibraryRootFolderID());
        if (lib_folder)
        {
            lib_folder->setOpen(true);
        }

        // Auto close the user's my inventory folder
        LLFolderViewFolder* my_inv_folder =   getFolderByID(gInventory.getRootFolderID());
        if (my_inv_folder)
        {
            my_inv_folder->setOpenArrangeRecursively(false, LLFolderViewFolder::RECURSE_DOWN);
        }
    }
}

void LLInventoryPanel::initRootContent()
{
    LLUUID root_id = getRootFolderID();
    if (root_id.notNull())
    {
        buildNewViews(getRootFolderID());
    }
    else
    {
        // Default case: always add "My Inventory" root first, "Library" root second
        // If we run out of time, this still should create root folders
        buildNewViews(gInventory.getRootFolderID());        // My Inventory
        buildNewViews(gInventory.getLibraryRootFolderID()); // Library
    }
}


LLFolderViewFolder * LLInventoryPanel::createFolderViewFolder(LLInvFVBridge * bridge, bool allow_drop)
{
    LLFolderViewFolder::Params params(mParams.folder);

#ifndef LL_RELEASE_FOR_DOWNLOAD
    // Only usable for debug and first call has a large
    // overhead from search string construction.
    // As inventory names aren't unique and can change,
    // there is little we can use them for in release builds.
    params.name = bridge->getName();
#else
    // We don't have a source of unique names and inventory
    // items can reach millions in quantity, just use
    // a short descriptor
    params.name = "fld";
#endif
    params.root = mFolderRoot.get();
    params.listener = bridge;
    params.allow_drop = allow_drop;

    params.font_color = (bridge->isLibraryItem() ? sLibraryColor : sDefaultColor);
    params.font_highlight_color = (bridge->isLibraryItem() ? sLibraryColor : sDefaultHighlightColor);

    return LLUICtrlFactory::create<LLFolderViewFolder>(params);
}

LLFolderViewItem * LLInventoryPanel::createFolderViewItem(LLInvFVBridge * bridge)
{
    LLFolderViewItem::Params params(mParams.item);

#ifndef LL_RELEASE_FOR_DOWNLOAD
    // Only usable for debug and first call has a large
    // overhead from search string construction.
    // As inventory names aren't unique, are large and can change,
    // there is little we can use them for in release builds.
    // Prefer shorter
    params.name = bridge->getName();
#else
    // We don't have a source of unique names and inventory
    // items can reach millions in quantity, just use
    // a short descriptor
    params.name = "itm";
#endif
    params.creation_date = bridge->getCreationDate();
    params.root = mFolderRoot.get();
    params.listener = bridge;
    params.rect = LLRect (0, 0, 0, 0);

    params.font_color = (bridge->isLibraryItem() ? sLibraryColor : sDefaultColor);
    params.font_highlight_color = (bridge->isLibraryItem() ? sLibraryColor : sDefaultHighlightColor);

    return LLUICtrlFactory::create<LLFolderViewItem>(params);
}

LLFolderViewItem* LLInventoryPanel::buildNewViews(const LLUUID& id)
{
    LLInventoryObject const* objectp = mInventory->getObject(id);
    return buildNewViews(id, objectp);
}

LLFolderViewItem* LLInventoryPanel::buildNewViews(const LLUUID& id, LLInventoryObject const* objectp)
{
    if (!objectp)
    {
        return NULL;
    }
    if (!typedViewsFilter(id, objectp))
    {
        // if certain types are not allowed permanently, no reason to create views
        return NULL;
    }

    const LLUUID &parent_id = objectp->getParentUUID();
    LLFolderViewItem* folder_view_item = getItemByID(id);
    LLFolderViewFolder* parent_folder = (LLFolderViewFolder*)getItemByID(parent_id);

    return buildViewsTree(id, parent_id, objectp, folder_view_item, parent_folder, BUILD_TIMELIMIT);
}

LLFolderViewItem* LLInventoryPanel::buildNewViews(const LLUUID& id,
                                                  LLInventoryObject const* objectp,
                                                  LLFolderViewItem *folder_view_item,
                                                  const EBuildModes &mode)
{
    if (!objectp)
    {
        return NULL;
    }
    if (!typedViewsFilter(id, objectp))
    {
        // if certain types are not allowed permanently, no reason to create views
        return NULL;
    }

    const LLUUID &parent_id = objectp->getParentUUID();
    LLFolderViewFolder* parent_folder = (LLFolderViewFolder*)getItemByID(parent_id);

    return buildViewsTree(id, parent_id, objectp, folder_view_item, parent_folder, mode);
}

LLFolderViewItem* LLInventoryPanel::buildViewsTree(const LLUUID& id,
                                                  const LLUUID& parent_id,
                                                  LLInventoryObject const* objectp,
                                                  LLFolderViewItem *folder_view_item,
                                                  LLFolderViewFolder *parent_folder,
                                                  const EBuildModes &mode,
                                                  S32 depth)
{
    depth++;

    // Force the creation of an extra root level folder item if required by the inventory panel (default is "false")
    bool allow_drop = true;
    bool create_root = false;
    if (mParams.show_root_folder)
    {
        LLUUID root_id = getRootFolderID();
        if (root_id == id)
        {
            // We insert an extra level that's seen by the UI but has no influence on the model
            parent_folder = dynamic_cast<LLFolderViewFolder*>(folder_view_item);
            folder_view_item = NULL;
            allow_drop = mParams.allow_drop_on_root;
            create_root = true;
        }
    }

    if (!folder_view_item && parent_folder)
        {
            if (objectp->getType() <= LLAssetType::AT_NONE)
            {
                LL_WARNS() << "LLInventoryPanel::buildViewsTree called with invalid objectp->mType : "
                    << ((S32)objectp->getType()) << " name " << objectp->getName() << " UUID " << objectp->getUUID()
                    << LL_ENDL;
                return NULL;
            }

            if (objectp->getType() >= LLAssetType::AT_COUNT)
            {
                // Example: Happens when we add assets of new, not yet supported type to library
                LL_DEBUGS("Inventory") << "LLInventoryPanel::buildViewsTree called with unknown objectp->mType : "
                << ((S32) objectp->getType()) << " name " << objectp->getName() << " UUID " << objectp->getUUID()
                << LL_ENDL;

                LLInventoryItem* item = (LLInventoryItem*)objectp;
                if (item)
                {
                    LLInvFVBridge* new_listener = mInvFVBridgeBuilder->createBridge(LLAssetType::AT_UNKNOWN,
                        LLAssetType::AT_UNKNOWN,
                        LLInventoryType::IT_UNKNOWN,
                        this,
                        &mInventoryViewModel,
                        mFolderRoot.get(),
                        item->getUUID(),
                        item->getFlags());

                    if (new_listener)
                    {
                        folder_view_item = createFolderViewItem(new_listener);
                    }
                }
            }

            if ((objectp->getType() == LLAssetType::AT_CATEGORY) &&
                (objectp->getActualType() != LLAssetType::AT_LINK_FOLDER))
            {
                LLInvFVBridge* new_listener = mInvFVBridgeBuilder->createBridge(LLAssetType::AT_CATEGORY,
                                            (mParams.use_marketplace_folders ? LLAssetType::AT_MARKETPLACE_FOLDER : LLAssetType::AT_CATEGORY),
                                                                                LLInventoryType::IT_CATEGORY,
                                                                                this,
                                                                                &mInventoryViewModel,
                                                                                mFolderRoot.get(),
                                                                                objectp->getUUID());
                if (new_listener)
                {
                    folder_view_item = createFolderViewFolder(new_listener,allow_drop);
                }
            }
            else
            {
                // Build new view for item.
                LLInventoryItem* item = (LLInventoryItem*)objectp;
                LLInvFVBridge* new_listener = mInvFVBridgeBuilder->createBridge(item->getType(),
                                                                                item->getActualType(),
                                                                                item->getInventoryType(),
                                                                                this,
                                                                            &mInventoryViewModel,
                                                                                mFolderRoot.get(),
                                                                                item->getUUID(),
                                                                                item->getFlags());

                if (new_listener)
                {
                folder_view_item = createFolderViewItem(new_listener);
                }
            }

        if (folder_view_item)
        {
            llassert(parent_folder != NULL);
            folder_view_item->addToFolder(parent_folder);
            addItemID(id, folder_view_item);
            // In the case of the root folder been shown, open that folder by default once the widget is created
            if (create_root)
            {
                folder_view_item->setOpen(true);
            }
        }
    }

    bool create_children = folder_view_item && objectp->getType() == LLAssetType::AT_CATEGORY
                            && (mBuildChildrenViews || depth == 0);

    if (create_children)
    {
        switch (mode)
        {
            case BUILD_TIMELIMIT:
            {
                F64 curent_time = LLTimer::getTotalSeconds();
                // If function is out of time, we want to shedule it into mBuildViewsQueue
                // If we have time, no matter how little, create views for all children
                //
                // This creates children in 'bulk' to make sure folder has either
                // 'empty and incomplete' or 'complete' states with nothing in between.
                // Folders are marked as mIsFolderComplete == false by default,
                // later arrange() will update mIsFolderComplete by child count
                if (mBuildViewsEndTime < curent_time)
                {
                    create_children = false;
                    // run it again for the sake of creating children
                    if (mBuildChildrenViews || depth == 0)
                    {
                        mBuildViewsQueue.push_back(id);
                    }
                }
                else
                {
                    create_children = true;
                    folder_view_item->setChildrenInited(mBuildChildrenViews);
                }
                break;
            }
            case BUILD_NO_CHILDREN:
            {
                create_children = false;
                // run it to create children, current caller is only interested in current view
                if (mBuildChildrenViews || depth == 0)
                {
                    mBuildViewsQueue.push_back(id);
                }
                break;
            }
            case BUILD_ONE_FOLDER:
            {
                // This view loads chindren, following ones don't
                // Note: Might be better idea to do 'depth' instead,
                // It also will help to prioritize root folder's content
                create_children = true;
                folder_view_item->setChildrenInited(true);
                break;
            }
            case BUILD_NO_LIMIT:
            default:
            {
                // keep working till everything exists
                create_children = true;
                folder_view_item->setChildrenInited(true);
            }
        }
    }

    // If this is a folder, add the children of the folder and recursively add any
    // child folders.
    if (create_children)
    {
        LLViewerInventoryCategory::cat_array_t* categories;
        LLViewerInventoryItem::item_array_t* items;
        mInventory->lockDirectDescendentArrays(id, categories, items);

        // Make sure panel won't lock in a loop over existing items if
        // folder is enormous and at least some work gets done
        const S32 MIN_ITEMS_PER_CALL = 500;
        const S32 starting_item_count = static_cast<S32>(mItemMap.size());

        LLFolderViewFolder *parentp = dynamic_cast<LLFolderViewFolder*>(folder_view_item);
        bool done = true;

        if(categories)
        {
            bool has_folders = parentp->getFoldersCount() > 0;
            for (LLViewerInventoryCategory::cat_array_t::const_iterator cat_iter = categories->begin();
                 cat_iter != categories->end();
                 ++cat_iter)
            {
                const LLViewerInventoryCategory* cat = (*cat_iter);
                if (typedViewsFilter(cat->getUUID(), cat))
                {
                    if (has_folders)
                    {
                        // This can be optimized: we don't need to call getItemByID()
                        // each time, especially since content is growing, we can just
                        // iter over copy of mItemMap in some way
                        LLFolderViewItem* view_itemp = getItemByID(cat->getUUID());
                        buildViewsTree(cat->getUUID(), id, cat, view_itemp, parentp, (mode == BUILD_ONE_FOLDER ? BUILD_NO_CHILDREN : mode), depth);
                    }
                    else
                    {
                        buildViewsTree(cat->getUUID(), id, cat, NULL, parentp, (mode == BUILD_ONE_FOLDER ? BUILD_NO_CHILDREN : mode), depth);
                    }
                }

                if (!mBuildChildrenViews
                    && mode == BUILD_TIMELIMIT
                    && MIN_ITEMS_PER_CALL + starting_item_count < static_cast<S32>(mItemMap.size()))
                {
                    // Single folder view, check if we still have time
                    //
                    // Todo: make sure this causes no dupplciates, breaks nothing,
                    // especially filters and arrange
                    F64 curent_time = LLTimer::getTotalSeconds();
                    if (mBuildViewsEndTime < curent_time)
                    {
                        mBuildViewsQueue.push_back(id);
                        done = false;
                        break;
                    }
                }
            }
        }

        if(items)
        {
            for (LLViewerInventoryItem::item_array_t::const_iterator item_iter = items->begin();
                 item_iter != items->end();
                 ++item_iter)
            {
                // At the moment we have to build folder's items in bulk and ignore mBuildViewsEndTime
                const LLViewerInventoryItem* item = (*item_iter);
                if (typedViewsFilter(item->getUUID(), item))
                {
                    // This can be optimized: we don't need to call getItemByID()
                    // each time, especially since content is growing, we can just
                    // iter over copy of mItemMap in some way
                    LLFolderViewItem* view_itemp = getItemByID(item->getUUID());
                    buildViewsTree(item->getUUID(), id, item, view_itemp, parentp, mode, depth);
                }

                if (!mBuildChildrenViews
                    && mode == BUILD_TIMELIMIT
                    && MIN_ITEMS_PER_CALL + starting_item_count < mItemMap.size())
                {
                    // Single folder view, check if we still have time
                    //
                    // Todo: make sure this causes no dupplciates, breaks nothing,
                    // especially filters and arrange
                    F64 curent_time = LLTimer::getTotalSeconds();
                    if (mBuildViewsEndTime < curent_time)
                    {
                        mBuildViewsQueue.push_back(id);
                        done = false;
                        break;
                    }
                }
            }
        }

        if (!mBuildChildrenViews && done)
        {
            // flat list is done initializing folder
            folder_view_item->setChildrenInited(true);
        }
        mInventory->unlockDirectDescendentArrays(id);
    }

    return folder_view_item;
}

// bit of a hack to make sure the inventory is open.
void LLInventoryPanel::openStartFolderOrMyInventory()
{
    // Find My Inventory folder and open it up by name
    for (LLView *child = mFolderRoot.get()->getFirstChild(); child; child = mFolderRoot.get()->findNextSibling(child))
    {
        LLFolderViewFolder *fchild = dynamic_cast<LLFolderViewFolder*>(child);
        if (fchild
            && fchild->getViewModelItem()
            // Is this right? Name might be localized,
            // use FT_ROOT_INVENTORY or gInventory.getRootFolderID()?
            && fchild->getViewModelItem()->getName() == "My Inventory")
        {
            fchild->setOpen(true);
            break;
        }
    }
}

void LLInventoryPanel::onItemsCompletion()
{
    if (mFolderRoot.get()) mFolderRoot.get()->updateMenu();
}

void LLInventoryPanel::openSelected()
{
    LLFolderViewItem* folder_item = mFolderRoot.get()->getCurSelectedItem();
    if(!folder_item) return;
    LLInvFVBridge* bridge = (LLInvFVBridge*)folder_item->getViewModelItem();
    if(!bridge) return;
    bridge->openItem();
}

void LLInventoryPanel::unSelectAll()
{
    mFolderRoot.get()->setSelection(NULL, false, false);
}


bool LLInventoryPanel::handleHover(S32 x, S32 y, MASK mask)
{
    bool handled = LLView::handleHover(x, y, mask);
    if(handled)
    {
        // getCursor gets current cursor, setCursor sets next cursor
        // check that children didn't set own 'next' cursor
        ECursorType cursor = getWindow()->getNextCursor();
        if (LLInventoryModelBackgroundFetch::instance().folderFetchActive() && cursor == UI_CURSOR_ARROW)
        {
            // replace arrow cursor with arrow and hourglass cursor
            getWindow()->setCursor(UI_CURSOR_WORKING);
        }
    }
    else
    {
        getWindow()->setCursor(UI_CURSOR_ARROW);
    }
    return true;
}

bool LLInventoryPanel::handleToolTip(S32 x, S32 y, MASK mask)
{
    if (const LLFolderViewItem* hover_item_p = (!mFolderRoot.isDead()) ? mFolderRoot.get()->getHoveredItem() : nullptr)
    {
        if (const LLFolderViewModelItemInventory* vm_item_p = static_cast<const LLFolderViewModelItemInventory*>(hover_item_p->getViewModelItem()))
        {
            LLSD params;
            params["inv_type"] = vm_item_p->getInventoryType();
            params["thumbnail_id"] = vm_item_p->getThumbnailUUID();
            params["item_id"] = vm_item_p->getUUID();

            // tooltip should only show over folder, but screen
            // rect includes items under folder as well
            LLRect actionable_rect = hover_item_p->calcScreenRect();
            if (hover_item_p->isOpen() && hover_item_p->hasVisibleChildren())
            {
                actionable_rect.mBottom = actionable_rect.mTop - hover_item_p->getItemHeight();
            }

            LLToolTipMgr::instance().show(LLToolTip::Params()
                    .message(hover_item_p->getToolTip())
                    .sticky_rect(actionable_rect)
                    .delay_time(LLView::getTooltipTimeout())
                    .create_callback(boost::bind(&LLInspectTextureUtil::createInventoryToolTip, _1))
                    .create_params(params));
            return true;
        }
    }
    return LLPanel::handleToolTip(x, y, mask);
}

bool LLInventoryPanel::handleDragAndDrop(S32 x, S32 y, MASK mask, bool drop,
                                   EDragAndDropType cargo_type,
                                   void* cargo_data,
                                   EAcceptance* accept,
                                   std::string& tooltip_msg)
{
    bool handled = false;

    if (mAcceptsDragAndDrop)
    {
        handled = LLPanel::handleDragAndDrop(x, y, mask, drop, cargo_type, cargo_data, accept, tooltip_msg);

        // If folder view is empty the (x, y) point won't be in its rect
        // so the handler must be called explicitly.
        // but only if was not handled before. See EXT-6746.
        if (!handled && mParams.allow_drop_on_root && !mFolderRoot.get()->hasVisibleChildren())
        {
            handled = mFolderRoot.get()->handleDragAndDrop(x, y, mask, drop, cargo_type, cargo_data, accept, tooltip_msg);
        }

        if (handled)
        {
            mFolderRoot.get()->setDragAndDropThisFrame();
        }
    }

    return handled;
}

void LLInventoryPanel::onFocusLost()
{
    // inventory no longer handles cut/copy/paste/delete
    if (LLEditMenuHandler::gEditMenuHandler == mFolderRoot.get())
    {
        LLEditMenuHandler::gEditMenuHandler = NULL;
    }

    LLPanel::onFocusLost();
}

void LLInventoryPanel::onFocusReceived()
{
    // inventory now handles cut/copy/paste/delete
    LLEditMenuHandler::gEditMenuHandler = mFolderRoot.get();

    LLPanel::onFocusReceived();
}

void LLInventoryPanel::onFolderOpening(const LLUUID &id)
{
    LLFolderViewItem* folder = getItemByID(id);
    if (folder && !folder->areChildrenInited())
    {
        // Last item in list will be processed first.
        // This might result in dupplicates in list, but it
        // isn't critical, views won't be created twice
        mBuildViewsQueue.push_back(id);
    }
}

bool LLInventoryPanel::addBadge(LLBadge * badge)
{
    bool badge_added = false;

    if (acceptsBadge())
    {
        badge_added = badge->addToView(mFolderRoot.get());
    }

    return badge_added;
}

void LLInventoryPanel::openAllFolders()
{
    mFolderRoot.get()->setOpenArrangeRecursively(true, LLFolderViewFolder::RECURSE_DOWN);
    mFolderRoot.get()->arrangeAll();
}

void LLInventoryPanel::setSelection(const LLUUID& obj_id, bool take_keyboard_focus)
{
    // Don't select objects in COF (e.g. to prevent refocus when items are worn).
    const LLInventoryObject *obj = mInventory->getObject(obj_id);
    if (obj && obj->getParentUUID() == LLAppearanceMgr::instance().getCOF())
    {
        return;
    }
    setSelectionByID(obj_id, take_keyboard_focus);
}

void LLInventoryPanel::setSelectCallback(const std::function<void (const std::deque<LLFolderViewItem*>& items, bool user_action)>& cb)
{
    if (mFolderRoot.get())
    {
        mFolderRoot.get()->setSelectCallback(cb);
    }
    mSelectionCallback = cb;
}

void LLInventoryPanel::clearSelection()
{
    mSelectThisID.setNull();
    mFocusSelection = false;
}

LLInventoryPanel::selected_items_t LLInventoryPanel::getSelectedItems() const
{
    return mFolderRoot.get()->getSelectionList();
}

void LLInventoryPanel::onSelectionChange(const std::deque<LLFolderViewItem*>& items, bool user_action)
{
    // Schedule updating the folder view context menu when all selected items become complete (STORM-373).
    mCompletionObserver->reset();
    for (std::deque<LLFolderViewItem*>::const_iterator it = items.begin(); it != items.end(); ++it)
    {
        LLFolderViewModelItemInventory* view_model = static_cast<LLFolderViewModelItemInventory*>((*it)->getViewModelItem());
        if (view_model)
        {
            LLUUID id = view_model->getUUID();
            if (!(*it)->areChildrenInited())
            {
                const F64 max_time = 0.0001f;
                mBuildViewsEndTime = LLTimer::getTotalSeconds() + max_time;
                buildNewViews(id);
            }
            LLViewerInventoryItem* inv_item = mInventory->getItem(id);

            if (inv_item && !inv_item->isFinished())
            {
                mCompletionObserver->watchItem(id);
            }
        }
    }

    LLFolderView* fv = mFolderRoot.get();
    if (fv->needsAutoRename()) // auto-selecting a new user-created asset and preparing to rename
    {
        fv->setNeedsAutoRename(false);
        if (items.size()) // new asset is visible and selected
        {
            fv->startRenamingSelectedItem();
        }
        else
        {
            LL_DEBUGS("Inventory") << "Failed to start renemr, no items selected" << LL_ENDL;
        }
    }

    std::set<LLFolderViewItem*> selected_items = mFolderRoot.get()->getSelectionList();
    LLFolderViewItem* prev_folder_item = getItemByID(mPreviousSelectedFolder);

    if (selected_items.size() == 1)
    {
        std::set<LLFolderViewItem*>::const_iterator iter = selected_items.begin();
        LLFolderViewItem* folder_item = (*iter);
        if(folder_item && (folder_item != prev_folder_item))
        {
            LLFolderViewModelItemInventory* fve_listener = static_cast<LLFolderViewModelItemInventory*>(folder_item->getViewModelItem());
            if (fve_listener && (fve_listener->getInventoryType() == LLInventoryType::IT_CATEGORY))
            {
                if (fve_listener->getInventoryObject() && fve_listener->getInventoryObject()->getIsLinkType())
                {
                    return;
                }

                if(prev_folder_item)
                {
                    LLFolderBridge* prev_bridge = (LLFolderBridge*)prev_folder_item->getViewModelItem();
                    if(prev_bridge)
                    {
                        prev_bridge->clearSearchableName();
                        prev_bridge->setShowDescendantsCount(false);
                        prev_folder_item->refresh();
                    }
                }

                LLFolderBridge* bridge = (LLFolderBridge*)folder_item->getViewModelItem();
                if(bridge)
                {
                    bridge->clearSearchableName();
                    bridge->setShowDescendantsCount(true);
                    folder_item->refresh();
                    mPreviousSelectedFolder = bridge->getUUID();
                }
            }
        }
    }
    else
    {
        if(prev_folder_item)
        {
            LLFolderBridge* prev_bridge = (LLFolderBridge*)prev_folder_item->getViewModelItem();
            if(prev_bridge)
            {
                prev_bridge->clearSearchableName();
                prev_bridge->setShowDescendantsCount(false);
                prev_folder_item->refresh();
            }
        }
        mPreviousSelectedFolder = LLUUID();
    }

}

void LLInventoryPanel::updateFolderLabel(const LLUUID& folder_id)
{
    if(folder_id != mPreviousSelectedFolder) return;

    LLFolderViewItem* folder_item = getItemByID(mPreviousSelectedFolder);
    if(folder_item)
    {
        LLFolderBridge* bridge = (LLFolderBridge*)folder_item->getViewModelItem();
        if(bridge)
        {
            bridge->clearSearchableName();
            bridge->setShowDescendantsCount(true);
            folder_item->refresh();
        }
    }
}

void LLInventoryPanel::doCreate(const LLSD& userdata)
{
    reset_inventory_filter();
    menu_create_inventory_item(this, LLFolderBridge::sSelf.get(), userdata);
}

bool LLInventoryPanel::beginIMSession()
{
    std::set<LLFolderViewItem*> selected_items =   mFolderRoot.get()->getSelectionList();

    std::string name;

    std::vector<LLUUID> members;
    EInstantMessage type = IM_SESSION_CONFERENCE_START;

    std::set<LLFolderViewItem*>::const_iterator iter;
    for (iter = selected_items.begin(); iter != selected_items.end(); iter++)
    {

        LLFolderViewItem* folder_item = (*iter);

        if(folder_item)
        {
            LLFolderViewModelItemInventory* fve_listener = static_cast<LLFolderViewModelItemInventory*>(folder_item->getViewModelItem());
            if (fve_listener && (fve_listener->getInventoryType() == LLInventoryType::IT_CATEGORY))
            {

                LLFolderBridge* bridge = (LLFolderBridge*)folder_item->getViewModelItem();
                if(!bridge) return true;
                LLViewerInventoryCategory* cat = bridge->getCategory();
                if(!cat) return true;
                name = cat->getName();
                LLUniqueBuddyCollector is_buddy;
                LLInventoryModel::cat_array_t cat_array;
                LLInventoryModel::item_array_t item_array;
                gInventory.collectDescendentsIf(bridge->getUUID(),
                                                cat_array,
                                                item_array,
                                                LLInventoryModel::EXCLUDE_TRASH,
                                                is_buddy);
                auto count = item_array.size();
                if(count > 0)
                {
                    //*TODO by what to replace that?
                    //LLFloaterReg::showInstance("communicate");

                    // create the session
                    LLAvatarTracker& at = LLAvatarTracker::instance();
                    LLUUID id;
                    for(size_t i = 0; i < count; ++i)
                    {
                        id = item_array.at(i)->getCreatorUUID();
                        if(at.isBuddyOnline(id))
                        {
                            members.push_back(id);
                        }
                    }
                }
            }
            else
            {
                LLInvFVBridge* listenerp = (LLInvFVBridge*)folder_item->getViewModelItem();

                if (listenerp->getInventoryType() == LLInventoryType::IT_CALLINGCARD)
                {
                    LLInventoryItem* inv_item = gInventory.getItem(listenerp->getUUID());

                    if (inv_item)
                    {
                        LLAvatarTracker& at = LLAvatarTracker::instance();
                        LLUUID id = inv_item->getCreatorUUID();

                        if(at.isBuddyOnline(id))
                        {
                            members.push_back(id);
                        }
                    }
                } //if IT_CALLINGCARD
            } //if !IT_CATEGORY
        }
    } //for selected_items

    // the session_id is randomly generated UUID which will be replaced later
    // with a server side generated number

    if (name.empty())
    {
        name = LLTrans::getString("conference-title");
    }

    LLUUID session_id = gIMMgr->addSession(name, type, members[0], members);
    if (session_id != LLUUID::null)
    {
        LLFloaterIMContainer::getInstance()->showConversation(session_id);
    }

    return true;
}

void LLInventoryPanel::fileUploadLocation(const LLSD& userdata)
{
    const std::string param = userdata.asString();
    const LLUUID dest = LLFolderBridge::sSelf.get()->getUUID();
    LLInventoryAction::fileUploadLocation(dest, param);
}

bool LLInventoryPanel::isUploadLocationSelected(const LLSD& userdata)
{
    const std::string param = userdata.asString();
    const LLUUID dest = LLFolderBridge::sSelf.get()->getUUID();
    return LLInventoryAction::isFileUploadLocation(dest, param);
}

void LLInventoryPanel::openSingleViewInventory(LLUUID folder_id)
{
    LLPanelMainInventory::newFolderWindow(folder_id.isNull() ? LLFolderBridge::sSelf.get()->getUUID() : folder_id);
}

void LLInventoryPanel::purgeSelectedItems()
{
    if (!mFolderRoot.get()) return;

    const LLUUID trash_id = gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH);
    const std::set<LLFolderViewItem*> inventory_selected = mFolderRoot.get()->getSelectionList();
    if (inventory_selected.empty()) return;
    LLSD args;
    auto count = inventory_selected.size();
    std::vector<LLUUID> selected_items;
    for (std::set<LLFolderViewItem*>::const_iterator it = inventory_selected.begin(), end_it = inventory_selected.end();
        it != end_it;
        ++it)
    {
        // Selection allows items outside trash folder, only count the ones inside.
        LLUUID item_id = static_cast<LLFolderViewModelItemInventory*>((*it)->getViewModelItem())->getUUID();
        LLInventoryObject* obj = gInventory.getObject(item_id);
        if (obj->getParentUUID() == trash_id)
        {
            LLInventoryModel::cat_array_t cats;
            LLInventoryModel::item_array_t items;
            gInventory.collectDescendents(item_id, cats, items, LLInventoryModel::INCLUDE_TRASH);
            count += items.size() + cats.size();
            selected_items.push_back(item_id);
        }
    }
    args["COUNT"] = static_cast<S32>(count);
    LLNotificationsUtil::add("PurgeSelectedItems", args, LLSD(), boost::bind(callbackPurgeSelectedItems, _1, _2, selected_items));
}

// static
void LLInventoryPanel::callbackPurgeSelectedItems(const LLSD& notification, const LLSD& response, const std::vector<LLUUID> inventory_selected)
{
    S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
    if (option == 0)
    {
        if (inventory_selected.empty()) return;

        for (auto it : inventory_selected)
        {
            remove_inventory_object(it, NULL);
        }
    }
}

bool LLInventoryPanel::attachObject(const LLSD& userdata)
{
    // Copy selected item UUIDs to a vector.
    std::set<LLFolderViewItem*> selected_items = mFolderRoot.get()->getSelectionList();
    uuid_vec_t items;
    for (std::set<LLFolderViewItem*>::const_iterator set_iter = selected_items.begin();
         set_iter != selected_items.end();
         ++set_iter)
    {
        items.push_back(static_cast<LLFolderViewModelItemInventory*>((*set_iter)->getViewModelItem())->getUUID());
    }

    // Attach selected items.
    LLViewerAttachMenu::attachObjects(items, userdata.asString());

    gFocusMgr.setKeyboardFocus(NULL);

    return true;
}

bool LLInventoryPanel::getSinceLogoff()
{
    return getFilter().isSinceLogoff();
}

// DEBUG ONLY
// static
void LLInventoryPanel::dumpSelectionInformation(void* user_data)
{
    LLInventoryPanel* iv = (LLInventoryPanel*)user_data;
    iv->mFolderRoot.get()->dumpSelectionInformation();
}

bool is_inventorysp_active()
{
    LLSidepanelInventory *sidepanel_inventory = LLFloaterSidePanelContainer::getPanel<LLSidepanelInventory>("inventory");
    if (!sidepanel_inventory || !sidepanel_inventory->isInVisibleChain()) return false;
    return sidepanel_inventory->isMainInventoryPanelActive();
}

// static
LLInventoryPanel* LLInventoryPanel::getActiveInventoryPanel(bool auto_open)
{
    S32 z_min = S32_MAX;
    LLInventoryPanel* res = NULL;
    LLFloater* active_inv_floaterp = NULL;

    LLFloater* floater_inventory = LLFloaterReg::getInstance("inventory");
    if (!floater_inventory)
    {
        LL_WARNS() << "Could not find My Inventory floater" << LL_ENDL;
        return nullptr;
    }

    LLSidepanelInventory *inventory_panel = LLFloaterSidePanelContainer::getPanel<LLSidepanelInventory>("inventory");

    // Iterate through the inventory floaters and return whichever is on top.
    LLFloaterReg::const_instance_list_t& inst_list = LLFloaterReg::getFloaterList("inventory");
    for (LLFloaterReg::const_instance_list_t::const_iterator iter = inst_list.begin(); iter != inst_list.end(); ++iter)
    {
        LLFloaterSidePanelContainer* inventory_floater = dynamic_cast<LLFloaterSidePanelContainer*>(*iter);
        inventory_panel = inventory_floater->findChild<LLSidepanelInventory>("main_panel");

        if (inventory_floater && inventory_panel && inventory_floater->getVisible())
        {
            S32 z_order = gFloaterView->getZOrder(inventory_floater);
            if (z_order < z_min)
            {
                res = inventory_panel->getActivePanel();
                z_min = z_order;
                active_inv_floaterp = inventory_floater;
            }
        }
    }

    if (res)
    {
        // Make sure the floater is not minimized (STORM-438).
        if (active_inv_floaterp && active_inv_floaterp->isMinimized())
        {
            active_inv_floaterp->setMinimized(false);
        }
    }
    else if (auto_open)
    {
        floater_inventory->openFloater();

        res = inventory_panel->getActivePanel();
    }

    return res;
}

//static
void LLInventoryPanel::openInventoryPanelAndSetSelection(bool auto_open, const LLUUID& obj_id,
    bool use_main_panel, bool take_keyboard_focus, bool reset_filter)
{
    LLSidepanelInventory* sidepanel_inventory = LLFloaterSidePanelContainer::getPanel<LLSidepanelInventory>("inventory");
    sidepanel_inventory->showInventoryPanel();

    LLUUID cat_id = gInventory.findCategoryUUIDForType(LLFolderType::FT_INBOX);
    bool in_inbox = gInventory.isObjectDescendentOf(obj_id, cat_id);
    if (!in_inbox && use_main_panel)
    {
        sidepanel_inventory->selectAllItemsPanel();
    }

    if (!auto_open)
    {
        LLFloater* inventory_floater = LLFloaterSidePanelContainer::getTopmostInventoryFloater();
        if (inventory_floater && inventory_floater->getVisible())
        {
            LLSidepanelInventory *inventory_panel = inventory_floater->findChild<LLSidepanelInventory>("main_panel");
            LLPanelMainInventory* main_panel = inventory_panel->getMainInventoryPanel();
            if (main_panel->isSingleFolderMode() && main_panel->isGalleryViewMode())
            {
                LL_DEBUGS("Inventory") << "Opening gallery panel for item" << obj_id << LL_ENDL;
                main_panel->setGallerySelection(obj_id);
                return;
            }
        }
    }

    if (use_main_panel)
    {
        LLPanelMainInventory* main_inventory = sidepanel_inventory->getMainInventoryPanel();
        if (main_inventory && main_inventory->isSingleFolderMode())
        {
            const LLInventoryObject *obj = gInventory.getObject(obj_id);
            if (obj)
            {
                LL_DEBUGS("Inventory") << "Opening main inventory panel for item" << obj_id << LL_ENDL;
                main_inventory->setSingleFolderViewRoot(obj->getParentUUID(), false);
                main_inventory->setGallerySelection(obj_id);
                return;
            }
        }
    }

    LLInventoryPanel *active_panel = LLInventoryPanel::getActiveInventoryPanel(auto_open);
    if (active_panel)
    {
        LL_DEBUGS("Messaging", "Inventory") << "Highlighting" << obj_id  << LL_ENDL;

        if (reset_filter)
        {
            reset_inventory_filter();
        }

        if (in_inbox)
        {
            sidepanel_inventory->openInbox();
            LLInventoryPanel* inventory_panel = sidepanel_inventory->getInboxPanel();
            if (inventory_panel)
            {
                inventory_panel->setSelection(obj_id, take_keyboard_focus);
            }
        }
        else if (auto_open)
        {
            LLFloater* floater_inventory = LLFloaterReg::getInstance("inventory");
            if (floater_inventory)
            {
                floater_inventory->setFocus(true);
            }
            active_panel->setSelection(obj_id, take_keyboard_focus);
        }
        else
        {
            // Created items are going to receive proper focus from callbacks
            active_panel->setSelection(obj_id, take_keyboard_focus);
        }
    }
}

void LLInventoryPanel::setSFViewAndOpenFolder(const LLInventoryPanel* panel, const LLUUID& folder_id)
{
    LLFloaterReg::const_instance_list_t& inst_list = LLFloaterReg::getFloaterList("inventory");
    for (LLFloaterReg::const_instance_list_t::const_iterator iter = inst_list.begin(); iter != inst_list.end(); ++iter)
    {
        LLFloaterSidePanelContainer* inventory_floater = dynamic_cast<LLFloaterSidePanelContainer*>(*iter);
        LLSidepanelInventory* sidepanel_inventory = inventory_floater->findChild<LLSidepanelInventory>("main_panel");

        LLPanelMainInventory* main_inventory = sidepanel_inventory->getMainInventoryPanel();
        if (main_inventory && panel->hasAncestor(main_inventory) && !main_inventory->isSingleFolderMode())
        {
            main_inventory->initSingleFolderRoot(folder_id);
            main_inventory->toggleViewMode();
            main_inventory->setSingleFolderViewRoot(folder_id, false);
        }
    }
}

void LLInventoryPanel::addHideFolderType(LLFolderType::EType folder_type)
{
    getFilter().setFilterCategoryTypes(getFilter().getFilterCategoryTypes() & ~(1ULL << folder_type));
}

bool LLInventoryPanel::getIsHiddenFolderType(LLFolderType::EType folder_type) const
{
    return !(getFilter().getFilterCategoryTypes() & (1ULL << folder_type));
}

void LLInventoryPanel::addItemID( const LLUUID& id, LLFolderViewItem*   itemp )
{
    mItemMap[id] = itemp;
}

void LLInventoryPanel::removeItemID(const LLUUID& id)
{
    LLInventoryModel::cat_array_t categories;
    LLInventoryModel::item_array_t items;
    gInventory.collectDescendents(id, categories, items, true);

    mItemMap.erase(id);

    for (LLInventoryModel::cat_array_t::iterator it = categories.begin(),    end_it = categories.end();
        it != end_it;
        ++it)
    {
        mItemMap.erase((*it)->getUUID());
}

    for (LLInventoryModel::item_array_t::iterator it = items.begin(),   end_it  = items.end();
        it != end_it;
        ++it)
    {
        mItemMap.erase((*it)->getUUID());
    }
}

LLFolderViewItem* LLInventoryPanel::getItemByID(const LLUUID& id)
{
    LL_PROFILE_ZONE_SCOPED;

    auto map_it = mItemMap.find(id);
    if (map_it != mItemMap.end())
    {
        return map_it->second;
    }

    return NULL;
}

LLFolderViewFolder* LLInventoryPanel::getFolderByID(const LLUUID& id)
{
    LLFolderViewItem* item = getItemByID(id);
    return dynamic_cast<LLFolderViewFolder*>(item);
}


void LLInventoryPanel::setSelectionByID( const LLUUID& obj_id, bool    take_keyboard_focus )
{
    LLFolderViewItem* itemp = getItemByID(obj_id);

    if (itemp && !itemp->areChildrenInited())
    {
        LLInventoryObject const* objectp = mInventory->getObject(obj_id);
        if (objectp)
        {
            buildNewViews(obj_id, objectp, itemp, BUILD_ONE_FOLDER);
        }
    }

    if(itemp && itemp->getViewModelItem())
    {
        itemp->arrangeAndSet(true, take_keyboard_focus);
        mSelectThisID.setNull();
        mFocusSelection = false;
        return;
    }
    else
    {
        // save the desired item to be selected later (if/when ready)
        mFocusSelection = take_keyboard_focus;
        mSelectThisID = obj_id;
    }
}

void LLInventoryPanel::updateSelection()
{
    if (mSelectThisID.notNull())
    {
        setSelectionByID(mSelectThisID, mFocusSelection);
    }
}

void LLInventoryPanel::doToSelected(const LLSD& userdata)
{
    if (("purge" == userdata.asString()))
    {
        purgeSelectedItems();
        return;
    }
    LLInventoryAction::doToSelected(mInventory, mFolderRoot.get(), userdata.asString());

    return;
}

bool LLInventoryPanel::handleKeyHere( KEY key, MASK mask )
{
    bool handled = false;
    switch (key)
    {
    case KEY_RETURN:
        // Open selected items if enter key hit on the inventory panel
        if (mask == MASK_NONE)
        {
            if (mSuppressOpenItemAction)
            {
                LLFolderViewItem* folder_item = mFolderRoot.get()->getCurSelectedItem();
                if(folder_item)
                {
                    LLInvFVBridge* bridge = (LLInvFVBridge*)folder_item->getViewModelItem();
                    if(bridge && (bridge->getInventoryType() != LLInventoryType::IT_CATEGORY))
                    {
                        return handled;
                    }
                }
            }
            LLInventoryAction::doToSelected(mInventory, mFolderRoot.get(), "open");
            handled = true;
        }
        break;
    case KEY_DELETE:
#if LL_DARWIN
    case KEY_BACKSPACE:
#endif
        // Delete selected items if delete or backspace key hit on the inventory panel
        // Note: on Mac laptop keyboards, backspace and delete are one and the same
        if (isSelectionRemovable() && (mask == MASK_NONE))
        {
            LLInventoryAction::doToSelected(mInventory, mFolderRoot.get(), "delete");
            handled = true;
        }
        break;
    }
    return handled;
}

bool LLInventoryPanel::isSelectionRemovable()
{
    bool can_delete = false;
    if (mFolderRoot.get())
    {
        std::set<LLFolderViewItem*> selection_set = mFolderRoot.get()->getSelectionList();
        if (!selection_set.empty())
        {
            can_delete = true;
            for (std::set<LLFolderViewItem*>::iterator iter = selection_set.begin();
                 iter != selection_set.end();
                 ++iter)
            {
                LLFolderViewItem *item = *iter;
                const LLFolderViewModelItemInventory *listener = static_cast<const LLFolderViewModelItemInventory*>(item->getViewModelItem());
                if (!listener)
                {
                    can_delete = false;
                }
                else
                {
                    can_delete &= listener->isItemRemovable() && !listener->isItemInTrash();
                }
            }
        }
    }
    return can_delete;
}

/************************************************************************/
/* Recent Inventory Panel related class                                 */
/************************************************************************/
static const LLRecentInventoryBridgeBuilder RECENT_ITEMS_BUILDER;
class LLInventoryRecentItemsPanel : public LLInventoryPanel
{
public:
    struct Params : public LLInitParam::Block<Params, LLInventoryPanel::Params>
    {};

    void initFromParams(const Params& p)
    {
        LLInventoryPanel::initFromParams(p);
        // turn on inbox for recent items
        getFilter().setFilterCategoryTypes(getFilter().getFilterCategoryTypes() | (1ULL << LLFolderType::FT_INBOX));
        // turn off marketplace for recent items
        getFilter().setFilterNoMarketplaceFolder();
    }

protected:
    LLInventoryRecentItemsPanel (const Params&);
    friend class LLUICtrlFactory;
};

LLInventoryRecentItemsPanel::LLInventoryRecentItemsPanel( const Params& params)
: LLInventoryPanel(params)
{
    // replace bridge builder to have necessary View bridges.
    mInvFVBridgeBuilder = &RECENT_ITEMS_BUILDER;
}

/************************************************************************/
/* Creator Inventory Panel related classes                              */
/************************************************************************/

struct LLCreatorStoreNameCacheEntry
{
    std::string name;
    S32 score = 0;
    F64 last_checked = 0.0;
};

static std::map<LLUUID, LLCreatorStoreNameCacheEntry>
    sCreatorStoreNameCache;

static bool sCreatorStoreNameCacheLoaded = false;

static void loadCreatorStoreNameCache()
{
    if (sCreatorStoreNameCacheLoaded)
    {
        return;
    }

    sCreatorStoreNameCacheLoaded = true;
    sCreatorStoreNameCache.clear();

    const std::string saved =
        gSavedSettings.getString("CreatorViewAutoStoreCache");

    std::string::size_type start = 0;

    while (start < saved.size())
    {
        const std::string::size_type end =
            saved.find('\n', start);

        const std::string line =
            saved.substr(
                start,
                (end == std::string::npos)
                    ? std::string::npos
                    : end - start);

        if (!line.empty())
        {
            const std::string::size_type p1 =
                line.find('|');
            const std::string::size_type p2 =
                (p1 == std::string::npos)
                    ? std::string::npos
                    : line.find('|', p1 + 1);
            const std::string::size_type p3 =
                (p2 == std::string::npos)
                    ? std::string::npos
                    : line.find('|', p2 + 1);

            if (p1 != std::string::npos
                && p2 != std::string::npos
                && p3 != std::string::npos)
            {
                const LLUUID creator_id(
                    line.substr(0, p1));

                if (creator_id.notNull())
                {
                    LLCreatorStoreNameCacheEntry entry;

                    entry.score =
                        atoi(
                            line.substr(
                                p1 + 1,
                                p2 - p1 - 1).c_str());

                    entry.last_checked =
                        atof(
                            line.substr(
                                p2 + 1,
                                p3 - p2 - 1).c_str());

                    entry.name =
                        line.substr(p3 + 1);

                    sCreatorStoreNameCache[creator_id] =
                        entry;
                }
            }
        }

        if (end == std::string::npos)
        {
            break;
        }

        start = end + 1;
    }
}

static void saveCreatorStoreNameCache()
{
    std::string saved;
    bool first = true;

    for (const auto& cached :
         sCreatorStoreNameCache)
    {
        if (cached.first.isNull())
        {
            continue;
        }

        std::string safe_name =
            cached.second.name;

        for (char& c : safe_name)
        {
            if (c == '\r'
                || c == '\n'
                || c == '|')
            {
                c = ' ';
            }
        }

        if (!first)
        {
            saved += "\n";
        }

        saved += cached.first.asString();
        saved += "|";
        saved +=
            std::to_string(cached.second.score);
        saved += "|";
        saved +=
            std::to_string(
                cached.second.last_checked);
        saved += "|";
        saved += safe_name;

        first = false;
    }

    gSavedSettings.setString(
        "CreatorViewAutoStoreCache",
        saved);
}

class LLCreatorVirtualFolderBridge final
    : public LLFolderBridge,
      public LLAvatarPropertiesObserver
{
public:
    LLCreatorVirtualFolderBridge(
        LLInventoryPanel* inventory,
        LLFolderView* root,
        const LLUUID& group_id,
        const LLUUID& creator_id)
        : LLFolderBridge(inventory, root, group_id),
          mCreatorID(creator_id),
          mCreatorName(),
          mStoreAlias(),
          mAutoStoreName(),
          mAutoStoreScore(0),
          mAutoStoreLastChecked(0.0),
          mStoreLookupRequested(false),
          mBaseName(),
          mDisplayName(),
          mItemCount(0)
    {
        loadStoreAlias();
        loadCreatorStoreNameCache();

        if (mCreatorID.notNull())
        {
            auto cached =
                sCreatorStoreNameCache.find(
                    mCreatorID);

            if (cached
                != sCreatorStoreNameCache.end())
            {
                mAutoStoreName =
                    cached->second.name;
                mAutoStoreScore =
                    cached->second.score;
                mAutoStoreLastChecked =
                    cached->second.last_checked;
            }

            LLAvatarPropertiesProcessor::getInstance()->addObserver(
                mCreatorID,
                this);
        }

        refreshCreatorName(false);
        updateDisplayName();
    }

    ~LLCreatorVirtualFolderBridge() override
    {
        if (mCreatorID.notNull())
        {
            LLAvatarPropertiesProcessor::getInstance()->removeObserver(
                mCreatorID,
                this);
        }
    }

    void processProperties(
        void* data,
        EAvatarProcessorType type) override
    {
        if (!data || mCreatorID.isNull())
        {
            return;
        }

        LLAvatarPropertiesProcessor* processor =
            LLAvatarPropertiesProcessor::getInstance();

        switch (type)
        {
            case APT_PROPERTIES:
            {
                LLAvatarData* avatar_data =
                    static_cast<LLAvatarData*>(data);

                if (avatar_data->avatar_id != mCreatorID)
                {
                    return;
                }

                markStoreLookupChecked();
                considerStoreText(avatar_data->about_text);

                for (const LLAvatarData::pick_data_t& pick :
                     avatar_data->picks_list)
                {
                    considerStoreText(pick.second);

                    if (mAutoStoreScore < 115
                        && mRequestedPickIDs.size() < 6
                        && mRequestedPickIDs.insert(pick.first).second)
                    {
                        processor->sendPickInfoRequest(
                            mCreatorID,
                            pick.first);
                    }
                }

                break;
            }

            case APT_CLASSIFIEDS:
            {
                LLAvatarClassifieds* classifieds =
                    static_cast<LLAvatarClassifieds*>(data);

                if (classifieds->target_id != mCreatorID)
                {
                    return;
                }

                markStoreLookupChecked();

                for (const LLAvatarClassifieds::classified_data& classified :
                     classifieds->classifieds_list)
                {
                    considerStoreText(classified.name);

                    if (mAutoStoreScore < 115
                        && mRequestedClassifiedIDs.size() < 6
                        && mRequestedClassifiedIDs.insert(
                            classified.classified_id).second)
                    {
                        processor->sendClassifiedInfoRequest(
                            classified.classified_id);
                    }
                }

                break;
            }

            case APT_PICK_INFO:
            {
                LLPickData* pick =
                    static_cast<LLPickData*>(data);

                if (pick->creator_id != mCreatorID)
                {
                    return;
                }

                considerStoreText(pick->name);
                considerStoreText(pick->desc);

                if (hasStoreSignal(pick->desc))
                {
                    considerStoreCandidate(pick->name, 90);
                }

                break;
            }

            case APT_CLASSIFIED_INFO:
            {
                LLAvatarClassifiedInfo* classified =
                    static_cast<LLAvatarClassifiedInfo*>(data);

                if (classified->creator_id != mCreatorID)
                {
                    return;
                }

                considerStoreText(classified->name);
                considerStoreText(classified->description);

                if (hasStoreSignal(classified->description))
                {
                    considerStoreCandidate(classified->name, 95);
                }

                break;
            }

            default:
                break;
        }
    }

    const std::string& getName() const override
    {
        refreshCreatorName();
        return mBaseName;
    }

    const std::string& getDisplayName() const override
    {
        refreshCreatorName();
        return mDisplayName;
    }

    std::string getSearchableCreatorName() const override
    {
        refreshCreatorName();

        std::string searchable = mStoreAlias;

        if (!mAutoStoreName.empty())
        {
            if (!searchable.empty())
            {
                searchable += " ";
            }
            searchable += mAutoStoreName;
        }

        if (!mCreatorName.empty())
        {
            if (!searchable.empty())
            {
                searchable += " ";
            }
            searchable += mCreatorName;
        }

        return searchable;
    }

    void buildSearchableName() const override
    {
        mSearchableName = getSearchableCreatorName();
        LLStringUtil::toUpper(mSearchableName);
    }

    LLFolderType::EType getPreferredType() const override
    {
        return LLFolderType::FT_NONE;
    }

    bool hasChildren() const override { return true; }
    bool isUpToDate() const override { return true; }

    bool isItemRenameable() const override
    {
        return mCreatorID.notNull();
    }

    bool renameItem(const std::string& new_name) override
    {
        if (mCreatorID.isNull() || new_name.empty())
        {
            return false;
        }

        mStoreAlias = new_name;
        saveStoreAlias();
        mSearchableName.clear();
        updateDisplayName();

        // This is a viewer-only synthetic folder. Unlike a real inventory
        // category, changing the alias does not generate an inventory-model
        // observer notification, so refresh the visible row explicitly.
        LLInventoryPanel* panel = mInventoryPanel.get();
        if (panel)
        {
            LLFolderViewItem* view_item = panel->getItemByID(mUUID);
            if (view_item)
            {
                view_item->refresh();

                LLFolderViewFolder* parent_folder =
                    view_item->getParentFolder();

                if (parent_folder)
                {
                    if (parent_folder->getViewModelItem())
                    {
                        parent_folder->getViewModelItem()->requestSort();
                    }
                    parent_folder->requestArrange();
                }
            }

            LLFolderView* root = panel->getRootFolder();
            if (root)
            {
                root->requestArrange();
            }
        }

        requestSort();
        return true;
    }

    bool removeItem() override { return false; }
    bool isItemRemovable(bool = true) const override { return false; }
    bool isItemMovable() const override { return false; }
    bool isItemCopyable(bool = true) const override { return false; }
    bool copyToClipboard() const override { return false; }
    bool cutToClipboard() override { return false; }
    bool isClipboardPasteable() const override { return false; }
    bool isClipboardPasteableAsLink() const override { return false; }
    void pasteFromClipboard() override {}
    void pasteLinkFromClipboard() override {}

    bool startDrag(EDragAndDropType* type, LLUUID* id) const override
    {
        if (type) *type = DAD_NONE;
        if (id) *id = LLUUID::null;
        return false;
    }

    bool dragOrDrop(MASK, bool, EDragAndDropType, void*, std::string&) override
    {
        return false;
    }

    void openItem() override {}
    void closeItem() override {}
    void selectItem() override {}
    void showProperties() override {}
    // Jackson Viewer: virtual store folder context menu.
    // This synthetic UUID is not a real LLInventoryCategory, so the stock
    // LLFolderBridge context-menu builder cannot discover Rename for it.
    void buildContextMenu(LLMenuGL& menu, U32 flags) override
    {
        menuentry_vec_t items;
        menuentry_vec_t disabled_items;

        items.push_back(std::string("Rename"));

        if (!isItemRenameable())
        {
            disabled_items.push_back(std::string("Rename"));
        }

        hide_context_entries(menu, items, disabled_items);

        menu.needsArrange();
        menu.arrangeAndClear();
    }

    bool refreshCreatorName(bool request_store = true) const
    {
        if (request_store)
        {
            maybeRequestStoreLookup();
        }

        std::string next_name;

        if (mCreatorID.isNull())
        {
            next_name = "Unknown Creator";
        }
        else
        {
            LLAvatarName av_name;
            if (LLAvatarNameCache::get(mCreatorID, &av_name))
            {
                next_name = av_name.getDisplayName();

                if (next_name.empty())
                {
                    next_name = av_name.getUserName();
                }
            }

            if (next_name.empty())
            {
                next_name = "Loading Creator...";
            }
        }

        if (next_name == mCreatorName)
        {
            return false;
        }

        mCreatorName = next_name;
        mSearchableName.clear();
        updateDisplayName();
        return true;
    }

    bool setItemCount(S32 item_count) const
    {
        if (item_count == mItemCount)
        {
            return false;
        }

        mItemCount = item_count;
        updateDisplayName();
        return true;
    }

private:
    static std::string normalizedStoreText(const std::string& value)
    {
        std::string normalized = value;
        LLStringUtil::toLower(normalized);
        return normalized;
    }

    static bool hasStoreSignal(const std::string& value)
    {
        const std::string lower = normalizedStoreText(value);

        return lower.find("mainstore") != std::string::npos
            || lower.find("main store") != std::string::npos
            || lower.find(" store") != std::string::npos
            || lower.find("store ") != std::string::npos
            || lower.find(" shop") != std::string::npos
            || lower.find("shop ") != std::string::npos
            || lower.find(" brand") != std::string::npos
            || lower.find("brand ") != std::string::npos
            || lower.find("marketplace") != std::string::npos;
    }

    static std::string cleanStoreCandidate(const std::string& raw)
    {
        std::string candidate = raw;
        LLStringUtil::trim(candidate);

        const std::string delimiters = "\r\n|;\t";
        const std::string::size_type cut =
            candidate.find_first_of(delimiters);

        if (cut != std::string::npos)
        {
            candidate = candidate.substr(0, cut);
            LLStringUtil::trim(candidate);
        }

        while (!candidate.empty())
        {
            const char c = candidate.front();
            if (c == '"' || c == '\'' || c == '[' || c == '('
                || c == '{' || c == ':' || c == '-' || c == '|')
            {
                candidate.erase(candidate.begin());
                LLStringUtil::trim(candidate);
            }
            else
            {
                break;
            }
        }

        while (!candidate.empty())
        {
            const char c = candidate.back();
            if (c == '"' || c == '\'' || c == ']' || c == ')'
                || c == '}' || c == ':' || c == '-' || c == '|'
                || c == ',' || c == ';' || c == '.' || c == '!')
            {
                candidate.pop_back();
                LLStringUtil::trim(candidate);
            }
            else
            {
                break;
            }
        }

        if (candidate.size() < 2 || candidate.size() > 64)
        {
            return std::string();
        }

        const std::string lower = normalizedStoreText(candidate);

        if (lower.find("http://") != std::string::npos
            || lower.find("https://") != std::string::npos
            || lower.find("www.") != std::string::npos
            || lower.find("secondlife://") != std::string::npos
            || lower.find("maps.secondlife.com") != std::string::npos
            || lower.find("marketplace.secondlife.com") != std::string::npos)
        {
            return std::string();
        }

        if (lower == "store"
            || lower == "mainstore"
            || lower == "main store"
            || lower == "shop"
            || lower == "brand"
            || lower == "marketplace"
            || lower == "second life"
            || lower == "secondlife"
            || lower == "here"
            || lower == "click here"
            || lower == "none"
            || lower == "n/a")
        {
            return std::string();
        }

        return candidate;
    }

    void refreshStoreDisplay()
    {
        mSearchableName.clear();
        updateDisplayName();

        LLInventoryPanel* panel = mInventoryPanel.get();
        if (!panel)
        {
            return;
        }

        LLFolderViewItem* view_item =
            panel->getItemByID(mUUID);

        if (view_item)
        {
            view_item->refresh();

            LLFolderViewFolder* parent_folder =
                view_item->getParentFolder();

            if (parent_folder)
            {
                if (parent_folder->getViewModelItem())
                {
                    parent_folder->getViewModelItem()->requestSort();
                }

                parent_folder->requestArrange();
            }
        }

        LLFolderView* root = panel->getRootFolder();
        if (root)
        {
            root->requestArrange();
        }

        requestSort();
    }

    void considerStoreCandidate(
        const std::string& raw_candidate,
        S32 score)
    {
        const std::string candidate =
            cleanStoreCandidate(raw_candidate);

        if (candidate.empty() || score <= mAutoStoreScore)
        {
            return;
        }

        mAutoStoreName = candidate;
        mAutoStoreScore = score;

        LLCreatorStoreNameCacheEntry& cached =
            sCreatorStoreNameCache[mCreatorID];

        cached.name = mAutoStoreName;
        cached.score = mAutoStoreScore;

        if (mAutoStoreLastChecked <= 0.0)
        {
            mAutoStoreLastChecked =
                LLDate::now().secondsSinceEpoch();
        }

        cached.last_checked =
            mAutoStoreLastChecked;

        saveCreatorStoreNameCache();
        refreshStoreDisplay();
    }

    void markStoreLookupChecked()
    {
        mAutoStoreLastChecked =
            LLDate::now().secondsSinceEpoch();

        LLCreatorStoreNameCacheEntry& cached =
            sCreatorStoreNameCache[mCreatorID];

        cached.name = mAutoStoreName;
        cached.score = mAutoStoreScore;
        cached.last_checked =
            mAutoStoreLastChecked;

        saveCreatorStoreNameCache();
    }

    void considerStoreText(const std::string& text)
    {
        if (text.empty())
        {
            return;
        }

        std::string normalized = text;
        std::replace(
            normalized.begin(),
            normalized.end(),
            '\r',
            '\n');

        std::string::size_type start = 0;

        while (start <= normalized.size())
        {
            const std::string::size_type end =
                normalized.find('\n', start);

            std::string line =
                normalized.substr(
                    start,
                    (end == std::string::npos)
                        ? std::string::npos
                        : end - start);

            LLStringUtil::trim(line);

            if (!line.empty())
            {
                const std::string lower =
                    normalizedStoreText(line);

                auto consider_after =
                    [&](const std::string& marker, S32 score) -> bool
                    {
                        const std::string::size_type pos =
                            lower.find(marker);

                        if (pos == std::string::npos)
                        {
                            return false;
                        }

                        considerStoreCandidate(
                            line.substr(pos + marker.size()),
                            score);
                        return true;
                    };

                if (consider_after("main store:", 120)
                    || consider_after("mainstore:", 120)
                    || consider_after("store name:", 118)
                    || consider_after("brand name:", 118)
                    || consider_after("store:", 115)
                    || consider_after("brand:", 115)
                    || consider_after("shop:", 112)
                    || consider_after("owner of ", 108)
                    || consider_after("creator of ", 108)
                    || consider_after("designer for ", 104))
                {
                    // Explicit store or brand label found on this line.
                }
                else
                {
                    const std::string suffixes[] =
                    {
                        " mainstore",
                        " main store"
                    };

                    for (const std::string& suffix : suffixes)
                    {
                        const std::string::size_type pos =
                            lower.find(suffix);

                        if (pos != std::string::npos && pos > 0)
                        {
                            considerStoreCandidate(
                                line.substr(0, pos),
                                100);
                            break;
                        }
                    }
                }
            }

            if (end == std::string::npos)
            {
                break;
            }

            start = end + 1;
        }
    }

    void maybeRequestStoreLookup() const
    {
        if (mCreatorID.isNull()
            || !mStoreAlias.empty()
            || mStoreLookupRequested
            || LLStartUp::getStartupState()
                <= STATE_AGENT_SEND)
        {
            return;
        }

        static const F64 CACHE_MAX_AGE_SECONDS =
            7.0 * 24.0 * 60.0 * 60.0;

        const F64 epoch_now =
            LLDate::now().secondsSinceEpoch();

        const bool cache_is_fresh =
            mAutoStoreLastChecked > 0.0
            && epoch_now >= mAutoStoreLastChecked
            && (epoch_now - mAutoStoreLastChecked)
                < CACHE_MAX_AGE_SECONDS;

        if (cache_is_fresh)
        {
            return;
        }

        static F64 next_request_time = 0.0;

        const F64 session_now =
            LLTimer::getTotalSeconds();

        if (session_now < next_request_time)
        {
            return;
        }

        next_request_time =
            session_now + 0.50;

        mStoreLookupRequested = true;

        LLAvatarPropertiesProcessor* processor =
            LLAvatarPropertiesProcessor::getInstance();

        processor->sendAvatarPropertiesRequest(
            mCreatorID);

        processor->sendAvatarClassifiedsRequest(
            mCreatorID);
    }

    void updateDisplayName() const
    {
        if (!mStoreAlias.empty())
        {
            mBaseName = mStoreAlias;
        }
        else if (!mAutoStoreName.empty() && mAutoStoreScore >= 100)
        {
            mBaseName = mAutoStoreName;
        }
        else
        {
            mBaseName = mCreatorName;
        }

        mDisplayName = mBaseName;
        mDisplayName += " (";
        mDisplayName += std::to_string(mItemCount);
        mDisplayName += ")";
    }

    void loadStoreAlias() const
    {
        mStoreAlias.clear();

        if (mCreatorID.isNull())
        {
            return;
        }

        const std::string saved =
            gSavedSettings.getString("CreatorViewStoreAliases");

        const std::string wanted = mCreatorID.asString() + "|";
        std::string::size_type start = 0;

        while (start < saved.size())
        {
            std::string::size_type end = saved.find('\n', start);
            std::string line = saved.substr(
                start,
                (end == std::string::npos)
                    ? std::string::npos
                    : end - start);

            if (line.compare(0, wanted.size(), wanted) == 0)
            {
                mStoreAlias = line.substr(wanted.size());
                return;
            }

            if (end == std::string::npos)
            {
                break;
            }

            start = end + 1;
        }
    }

    void saveStoreAlias() const
    {
        if (mCreatorID.isNull())
        {
            return;
        }

        const std::string saved =
            gSavedSettings.getString("CreatorViewStoreAliases");

        const std::string wanted = mCreatorID.asString() + "|";
        std::string rebuilt;
        std::string::size_type start = 0;

        while (start < saved.size())
        {
            std::string::size_type end = saved.find('\n', start);
            std::string line = saved.substr(
                start,
                (end == std::string::npos)
                    ? std::string::npos
                    : end - start);

            if (!line.empty()
                && line.compare(0, wanted.size(), wanted) != 0)
            {
                if (!rebuilt.empty())
                {
                    rebuilt += "\n";
                }
                rebuilt += line;
            }

            if (end == std::string::npos)
            {
                break;
            }

            start = end + 1;
        }

        if (!rebuilt.empty())
        {
            rebuilt += "\n";
        }

        rebuilt += wanted;
        rebuilt += mStoreAlias;

        gSavedSettings.setString("CreatorViewStoreAliases", rebuilt);
    }

private:
    LLUUID mCreatorID;
    mutable std::string mCreatorName;
    mutable std::string mStoreAlias;
    std::string mAutoStoreName;
    S32 mAutoStoreScore;
    F64 mAutoStoreLastChecked;
    mutable bool mStoreLookupRequested;
    std::set<LLUUID> mRequestedPickIDs;
    std::set<LLUUID> mRequestedClassifiedIDs;
    mutable std::string mBaseName;
    mutable std::string mDisplayName;
    mutable S32 mItemCount;
};


class LLCreatorTypeVirtualFolderBridge final : public LLFolderBridge
{
public:
    LLCreatorTypeVirtualFolderBridge(
        LLInventoryPanel* inventory,
        LLFolderView* root,
        const LLUUID& group_id,
        const std::string& label)
        : LLFolderBridge(inventory, root, group_id),
          mLabel(label),
          mDisplayName(label),
          mItemCount(0)
    {
        updateDisplayName();
    }

    const std::string& getName() const override { return mDisplayName; }
    const std::string& getDisplayName() const override { return mDisplayName; }

    void buildSearchableName() const override
    {
        mSearchableName = mLabel;
        LLStringUtil::toUpper(mSearchableName);
    }

    LLFolderType::EType getPreferredType() const override
    {
        return LLFolderType::FT_NONE;
    }

    bool hasChildren() const override { return true; }
    bool isUpToDate() const override { return true; }
    bool isItemRenameable() const override { return false; }
    bool renameItem(const std::string&) override { return false; }
    bool removeItem() override { return false; }
    bool isItemRemovable(bool = true) const override { return false; }
    bool isItemMovable() const override { return false; }
    bool isItemCopyable(bool = true) const override { return false; }
    bool copyToClipboard() const override { return false; }
    bool cutToClipboard() override { return false; }
    bool isClipboardPasteable() const override { return false; }
    bool isClipboardPasteableAsLink() const override { return false; }
    void pasteFromClipboard() override {}
    void pasteLinkFromClipboard() override {}

    bool startDrag(EDragAndDropType* type, LLUUID* id) const override
    {
        if (type) *type = DAD_NONE;
        if (id) *id = LLUUID::null;
        return false;
    }

    bool dragOrDrop(MASK, bool, EDragAndDropType, void*, std::string&) override
    {
        return false;
    }

    void openItem() override {}
    void closeItem() override {}
    void selectItem() override {}
    void showProperties() override {}

    bool setItemCount(S32 item_count) const
    {
        if (item_count == mItemCount)
        {
            return false;
        }

        mItemCount = item_count;
        updateDisplayName();
        return true;
    }

private:
    void updateDisplayName() const
    {
        mDisplayName = mLabel;
        mDisplayName += " (";
        mDisplayName += std::to_string(mItemCount);
        mDisplayName += ")";
    }

private:
    std::string mLabel;
    mutable std::string mDisplayName;
    mutable S32 mItemCount;
};


class LLInventoryCreatorItemsPanel : public LLInventoryPanel
{
public:
    struct Params : public LLInitParam::Block<Params, LLInventoryPanel::Params>
    {};

    void initFromParams(const Params& p)
    {
        LLInventoryPanel::initFromParams(p);

        getFilter().setFilterNoTrashFolder();
        getFilter().setFilterNoMarketplaceFolder();
        getFilter().setFilterLinks(LLInventoryFilter::FILTERLINK_EXCLUDE_LINKS);

        loadOpenState();
        loadTypeOpenState();
        loadClassificationOverrides();
    }

    bool showCreatorNamesInLabels() const override
    {
        return false;
    }

    void draw() override
    {
        bool creator_view_changed = false;
        bool open_state_changed = false;
        bool type_open_state_changed = false;

        for (auto& creator_entry : mCreatorFolders)
        {
            const LLUUID creator_id = creator_entry.first;
            LLFolderViewFolder* creator_folder = creator_entry.second;

            if (!creator_folder)
            {
                continue;
            }

            S32 total_items = 0;

            auto type_map_it = mTypeFolders.find(creator_id);
            if (type_map_it != mTypeFolders.end())
            {
                for (auto& type_entry : type_map_it->second)
                {
                    LLFolderViewFolder* type_folder = type_entry.second;
                    if (!type_folder)
                    {
                        continue;
                    }

                    S32 type_count = 0;

                    auto creator_subtypes =
                        mSubtypeFolders.find(creator_id);

                    if (creator_subtypes != mSubtypeFolders.end())
                    {
                        auto group_subtypes =
                            creator_subtypes->second.find(type_entry.first);

                        if (group_subtypes != creator_subtypes->second.end())
                        {
                            for (auto& subtype_entry : group_subtypes->second)
                            {
                                LLFolderViewFolder* subtype_folder =
                                    subtype_entry.second;

                                if (!subtype_folder)
                                {
                                    continue;
                                }

                                S32 subtype_count =
                                    static_cast<S32>(
                                        subtype_folder->getItemsCount());

                                auto creator_products =
                                    mProductFamilyFolders.find(creator_id);

                                if (creator_products
                                    != mProductFamilyFolders.end())
                                {
                                    auto group_products =
                                        creator_products->second.find(
                                            type_entry.first);

                                    if (group_products
                                        != creator_products->second.end())
                                    {
                                        auto subtype_products =
                                            group_products->second.find(
                                                subtype_entry.first);

                                        if (subtype_products
                                            != group_products->second.end())
                                        {
                                            for (auto& product_entry :
                                                 subtype_products->second)
                                            {
                                                LLFolderViewFolder* product_folder =
                                                    product_entry.second;

                                                if (!product_folder)
                                                {
                                                    continue;
                                                }

                                                const S32 product_count =
                                                    static_cast<S32>(
                                                        product_folder->
                                                            getItemsCount());

                                                subtype_count += product_count;

                                                LLCreatorTypeVirtualFolderBridge*
                                                    product_bridge =
                                                        dynamic_cast<
                                                            LLCreatorTypeVirtualFolderBridge*>(
                                                                product_folder->
                                                                    getViewModelItem());

                                                if (product_bridge
                                                    && product_bridge->
                                                        setItemCount(
                                                            product_count))
                                                {
                                                    product_folder->refresh();
                                                    product_folder->
                                                        getViewModelItem()->
                                                            dirtyFilter();
                                                    product_folder->
                                                        getViewModelItem()->
                                                            requestSort();
                                                    creator_view_changed = true;
                                                }

                                                const std::string
                                                    product_state_key =
                                                        productFamilyStateKey(
                                                            creator_id,
                                                            type_entry.first,
                                                            subtype_entry.first,
                                                            product_entry.first);

                                                const bool product_is_open =
                                                    product_folder->isOpen();

                                                const bool product_was_open =
                                                    (mOpenTypeFolderKeys.find(
                                                        product_state_key)
                                                        != mOpenTypeFolderKeys.end());

                                                if (product_is_open
                                                    != product_was_open)
                                                {
                                                    if (product_is_open)
                                                    {
                                                        mOpenTypeFolderKeys.insert(
                                                            product_state_key);
                                                    }
                                                    else
                                                    {
                                                        mOpenTypeFolderKeys.erase(
                                                            product_state_key);
                                                    }

                                                    type_open_state_changed = true;
                                                }
                                            }
                                        }
                                    }
                                }

                                type_count += subtype_count;

                                LLCreatorTypeVirtualFolderBridge* subtype_bridge =
                                    dynamic_cast<LLCreatorTypeVirtualFolderBridge*>(
                                        subtype_folder->getViewModelItem());

                                if (subtype_bridge
                                    && subtype_bridge->setItemCount(subtype_count))
                                {
                                    subtype_folder->refresh();
                                    subtype_folder->getViewModelItem()->dirtyFilter();
                                    subtype_folder->getViewModelItem()->requestSort();
                                    creator_view_changed = true;
                                }

                                const std::string subtype_state_key =
                                    subtypeStateKey(
                                        creator_id,
                                        type_entry.first,
                                        subtype_entry.first);

                                const bool subtype_is_open =
                                    subtype_folder->isOpen();

                                const bool subtype_was_open =
                                    (mOpenTypeFolderKeys.find(subtype_state_key)
                                        != mOpenTypeFolderKeys.end());

                                if (subtype_is_open != subtype_was_open)
                                {
                                    if (subtype_is_open)
                                    {
                                        mOpenTypeFolderKeys.insert(
                                            subtype_state_key);
                                    }
                                    else
                                    {
                                        mOpenTypeFolderKeys.erase(
                                            subtype_state_key);
                                    }

                                    type_open_state_changed = true;
                                }
                            }
                        }
                    }

                    total_items += type_count;

                    LLCreatorTypeVirtualFolderBridge* type_bridge =
                        dynamic_cast<LLCreatorTypeVirtualFolderBridge*>(
                            type_folder->getViewModelItem());

                    if (type_bridge && type_bridge->setItemCount(type_count))
                    {
                        type_folder->refresh();
                        type_folder->getViewModelItem()->dirtyFilter();
                        type_folder->getViewModelItem()->requestSort();
                        creator_view_changed = true;
                    }

                    const std::string type_state_key =
                        typeStateKey(creator_id, type_entry.first);

                    const bool type_is_open = type_folder->isOpen();
                    const bool type_was_open =
                        (mOpenTypeFolderKeys.find(type_state_key)
                            != mOpenTypeFolderKeys.end());

                    if (type_is_open != type_was_open)
                    {
                        if (type_is_open)
                        {
                            mOpenTypeFolderKeys.insert(type_state_key);
                        }
                        else
                        {
                            mOpenTypeFolderKeys.erase(type_state_key);
                        }

                        type_open_state_changed = true;
                    }
                }
            }

            LLCreatorVirtualFolderBridge* creator_bridge =
                dynamic_cast<LLCreatorVirtualFolderBridge*>(
                    creator_folder->getViewModelItem());

            if (creator_bridge)
            {
                if (creator_bridge->refreshCreatorName()
                    || creator_bridge->setItemCount(total_items))
                {
                    creator_folder->refresh();
                    creator_folder->getViewModelItem()->dirtyFilter();
                    creator_folder->getViewModelItem()->requestSort();
                    creator_view_changed = true;
                }
            }

            const LLUUID group_id = groupIDForCreator(creator_id);
            const bool is_open = creator_folder->isOpen();
            const bool was_open =
                (mOpenCreatorIDs.find(group_id) != mOpenCreatorIDs.end());

            if (is_open != was_open)
            {
                if (is_open)
                {
                    mOpenCreatorIDs.insert(group_id);
                }
                else
                {
                    mOpenCreatorIDs.erase(group_id);
                }

                open_state_changed = true;
            }
        }

        if (creator_view_changed && mFolderRoot.get())
        {
            if (mFolderRoot.get()->getViewModelItem())
            {
                mFolderRoot.get()->getViewModelItem()->requestSort();
            }

            mFolderRoot.get()->requestArrange();
        }

        if (open_state_changed)
        {
            saveOpenState();
        }

        if (type_open_state_changed)
        {
            saveTypeOpenState();
        }

        LLInventoryPanel::draw();
    }

    void doToSelected(const LLSD& userdata)
    {
        const std::string action = userdata.asString();

        if (action.find("creator_classify_") == 0)
        {
            applyClassificationAction(action);
            return;
        }

        LLInventoryPanel::doToSelected(userdata);
    }

protected:
    enum ECreatorItemGroup
    {
        CREATOR_GROUP_SHIRTS_TOPS = 0,
        CREATOR_GROUP_PANTS_BOTTOMS,
        CREATOR_GROUP_SHOES_FOOTWEAR,
        CREATOR_GROUP_JACKETS_OUTERWEAR,
        CREATOR_GROUP_SOCKS_HOSIERY,
        CREATOR_GROUP_GLOVES,
        CREATOR_GROUP_SKIRTS,
        CREATOR_GROUP_DRESSES_OUTFITS,
        CREATOR_GROUP_UNDERWEAR,
        CREATOR_GROUP_HAIR,
        CREATOR_GROUP_BODY_PARTS,
        CREATOR_GROUP_BODY_LAYERS,
        CREATOR_GROUP_ACCESSORIES,
        CREATOR_GROUP_OTHER_WEARABLES
    };

    LLInventoryCreatorItemsPanel(const Params& params)
        : LLInventoryPanel(params)
    {
        mCommitCallbackRegistrar.replace(
            "Inventory.DoToSelected",
            boost::bind(
                &LLInventoryCreatorItemsPanel::doToSelected,
                this,
                _2));
    }

    friend class LLUICtrlFactory;

    bool containsAny(
        const std::string& text,
        std::initializer_list<const char*> words) const
    {
        for (const char* word : words)
        {
            if (text.find(word) != std::string::npos)
            {
                return true;
            }
        }

        return false;
    }

    std::string normalizedItemName(
        const LLViewerInventoryItem* item) const
    {
        if (!item)
        {
            return std::string();
        }

        std::string result = " " + item->getName() + " ";
        LLStringUtil::toLower(result);
        return result;
    }

    std::string normalizedParentPath(
        const LLViewerInventoryItem* item) const
    {
        if (!item)
        {
            return std::string();
        }

        std::string result;
        LLUUID parent_id = item->getParentUUID();

        for (S32 depth = 0; depth < 6 && parent_id.notNull(); ++depth)
        {
            LLViewerInventoryCategory* category =
                mInventory->getCategory(parent_id);

            if (!category)
            {
                break;
            }

            result += " ";
            result += category->getName();
            result += " ";

            const LLUUID next_parent = category->getParentUUID();

            if (next_parent == parent_id
                || parent_id == mInventory->getRootFolderID())
            {
                break;
            }

            parent_id = next_parent;
        }

        LLStringUtil::toLower(result);
        return result;
    }

    S32 groupFromWearableType(
        LLWearableType::EType wearable_type) const
    {
        switch (wearable_type)
        {
            case LLWearableType::WT_SHIRT:
                return CREATOR_GROUP_SHIRTS_TOPS;
            case LLWearableType::WT_PANTS:
                return CREATOR_GROUP_PANTS_BOTTOMS;
            case LLWearableType::WT_SHOES:
                return CREATOR_GROUP_SHOES_FOOTWEAR;
            case LLWearableType::WT_SOCKS:
                return CREATOR_GROUP_SOCKS_HOSIERY;
            case LLWearableType::WT_JACKET:
                return CREATOR_GROUP_JACKETS_OUTERWEAR;
            case LLWearableType::WT_GLOVES:
                return CREATOR_GROUP_GLOVES;
            case LLWearableType::WT_SKIRT:
                return CREATOR_GROUP_SKIRTS;
            case LLWearableType::WT_UNDERSHIRT:
            case LLWearableType::WT_UNDERPANTS:
                return CREATOR_GROUP_UNDERWEAR;
            case LLWearableType::WT_HAIR:
                return CREATOR_GROUP_HAIR;
            case LLWearableType::WT_SHAPE:
            case LLWearableType::WT_SKIN:
            case LLWearableType::WT_EYES:
                return CREATOR_GROUP_BODY_PARTS;
            case LLWearableType::WT_ALPHA:
            case LLWearableType::WT_TATTOO:
            case LLWearableType::WT_PHYSICS:
            case LLWearableType::WT_UNIVERSAL:
                return CREATOR_GROUP_BODY_LAYERS;
            default:
                return CREATOR_GROUP_OTHER_WEARABLES;
        }
    }

    S32 groupFromObjectText(
        const std::string& text,
        bool allow_generic_outfit_word) const
    {
        if (containsAny(text, {" hud ", " applier ", " controller "}))
            return CREATOR_GROUP_ACCESSORIES;

        if (containsAny(text, {
            " shirt ", " shirts ", " top ", " tops ", " tee ", " tees ",
            " t-shirt ", " tshirt ", " blouse ", " blouses ", " tank ",
            " tanktop ", " tank top ", " crop top ", " sweater ",
            " sweatshirt ", " tunic ", " polo "
        }))
            return CREATOR_GROUP_SHIRTS_TOPS;

        if (containsAny(text, {
            " pants ", " pant ", " jeans ", " jean ", " trousers ",
            " trouser ", " leggings ", " legging ", " shorts ", " short ",
            " joggers ", " jogger ", " slacks ", " bottoms ", " bottom "
        }))
            return CREATOR_GROUP_PANTS_BOTTOMS;

        if (containsAny(text, {
            " shoes ", " shoe ", " heels ", " heel ", " boots ", " boot ",
            " sneakers ", " sneaker ", " sandals ", " sandal ", " pumps ",
            " pump ", " loafers ", " loafer ", " slippers ", " slipper ",
            " footwear "
        }))
            return CREATOR_GROUP_SHOES_FOOTWEAR;

        if (containsAny(text, {
            " jacket ", " jackets ", " coat ", " coats ", " blazer ",
            " blazers ", " hoodie ", " hoodies ", " cardigan ",
            " cardigans ", " parka ", " parkas ", " outerwear ",
            " overcoat ", " trench ", " bolero "
        }))
            return CREATOR_GROUP_JACKETS_OUTERWEAR;

        if (containsAny(text, {
            " socks ", " sock ", " stockings ", " stocking ",
            " hosiery ", " tights ", " thigh highs ", " thigh-highs "
        }))
            return CREATOR_GROUP_SOCKS_HOSIERY;

        if (containsAny(text, {
            " gloves ", " glove ", " mittens ", " mitten ",
            " gauntlets ", " gauntlet "
        }))
            return CREATOR_GROUP_GLOVES;

        if (containsAny(text, {
            " skirt ", " skirts ", " miniskirt ", " mini skirt "
        }))
            return CREATOR_GROUP_SKIRTS;

        if (containsAny(text, {
            " dress ", " dresses ", " gown ", " gowns ", " romper ",
            " rompers ", " jumpsuit ", " jumpsuits ", " catsuit ",
            " catsuits ", " bodysuit ", " bodysuits "
        }))
            return CREATOR_GROUP_DRESSES_OUTFITS;

        if (allow_generic_outfit_word
            && containsAny(text, {" outfit ", " outfits ", " ensemble "}))
            return CREATOR_GROUP_DRESSES_OUTFITS;

        if (containsAny(text, {
            " underwear ", " lingerie ", " bra ", " bras ", " panties ",
            " panty ", " thong ", " thongs ", " briefs ", " brief ",
            " boxers ", " boxer ", " underpants ", " undershirt ",
            " lingerie set "
        }))
            return CREATOR_GROUP_UNDERWEAR;

        if (containsAny(text, {
            " hair ", " hairs ", " wig ", " wigs ", " ponytail ",
            " ponytails ", " braid ", " braids ", " bun ", " buns ",
            " bangs ", " bang "
        }))
            return CREATOR_GROUP_HAIR;

        if (containsAny(text, {
            " body ", " bodies ", " mesh body ", " head ", " heads ",
            " mesh head ", " hands ", " hand ", " feet ", " foot ",
            " eyes ", " eye "
        }))
            return CREATOR_GROUP_BODY_PARTS;

        if (containsAny(text, {
            " tattoo ", " tattoos ", " alpha layer ", " bom layer ",
            " universal layer ", " body layer ", " skin layer "
        }))
            return CREATOR_GROUP_BODY_LAYERS;

        if (containsAny(text, {
            " necklace ", " necklaces ", " earring ", " earrings ",
            " bracelet ", " bracelets ", " ring ", " rings ", " choker ",
            " chokers ", " glasses ", " sunglasses ", " hat ", " hats ",
            " cap ", " caps ", " beanie ", " scarf ", " scarves ",
            " belt ", " belts ", " harness ", " harnesses ", " bag ",
            " bags ", " purse ", " purses ", " backpack ", " backpacks ",
            " mask ", " masks ", " watch ", " watches ", " collar ",
            " collars ", " nails ", " nail ", " piercing ", " piercings ",
            " horns ", " horn ", " wings ", " wing ", " tail ", " tails ",
            " accessory ", " accessories "
        }))
            return CREATOR_GROUP_ACCESSORIES;

        return -1;
    }

    static bool creatorProductBoundary(char c)
    {
        const unsigned char uc =
            static_cast<unsigned char>(c);

        return !std::isalnum(uc);
    }

    static bool stripCreatorProductVariantPhrase(
        std::string& value,
        const std::string& phrase)
    {
        if (value.empty() || phrase.empty())
        {
            return false;
        }

        bool removed = false;
        std::string lower = value;
        LLStringUtil::toLower(lower);

        std::string wanted = phrase;
        LLStringUtil::toLower(wanted);

        std::string::size_type search_from = 0;

        while (search_from < lower.size())
        {
            const std::string::size_type pos =
                lower.find(wanted, search_from);

            if (pos == std::string::npos)
            {
                break;
            }

            const std::string::size_type after =
                pos + wanted.size();

            const bool left_ok =
                (pos == 0)
                || creatorProductBoundary(lower[pos - 1]);

            const bool right_ok =
                (after >= lower.size())
                || creatorProductBoundary(lower[after]);

            if (left_ok && right_ok)
            {
                value.erase(pos, wanted.size());
                lower.erase(pos, wanted.size());
                removed = true;
                search_from = pos;
            }
            else
            {
                search_from = pos + wanted.size();
            }
        }

        return removed;
    }

    static std::string cleanCreatorProductFamilyLabel(
        const std::string& raw)
    {
        std::string label = raw;

        for (char& c : label)
        {
            if (c == '\t'
                || c == '[' || c == ']'
                || c == '(' || c == ')'
                || c == '{' || c == '}'
                || c == '|' || c == '/' || c == '\\'
                || c == '_' || c == '+')
            {
                c = ' ';
            }
        }

        std::string collapsed;
        bool previous_space = false;

        for (char c : label)
        {
            const bool is_space =
                std::isspace(static_cast<unsigned char>(c)) != 0;

            if (is_space)
            {
                if (!previous_space && !collapsed.empty())
                {
                    collapsed += ' ';
                }

                previous_space = true;
            }
            else
            {
                collapsed += c;
                previous_space = false;
            }
        }

        LLStringUtil::trim(collapsed);

        auto removable_edge =
            [](char c) -> bool
            {
                return c == '-'
                    || c == ':'
                    || c == '.'
                    || c == ','
                    || c == ';';
            };

        while (!collapsed.empty()
               && removable_edge(collapsed.front()))
        {
            collapsed.erase(collapsed.begin());
            LLStringUtil::trim(collapsed);
        }

        while (!collapsed.empty()
               && removable_edge(collapsed.back()))
        {
            collapsed.pop_back();
            LLStringUtil::trim(collapsed);
        }

        return collapsed;
    }

    static std::string creatorProductFamilyKey(
        const std::string& family_label)
    {
        std::string key = family_label;
        LLStringUtil::trim(key);
        LLStringUtil::toLower(key);
        return key;
    }

    std::string creatorProductFamilyLabelForItem(
        const LLViewerInventoryItem* item) const
    {
        if (!item)
        {
            return std::string();
        }

        std::string family = item->getName();
        LLStringUtil::trim(family);

        if (family.empty())
        {
            return std::string();
        }

        const std::string original = family;
        bool removed_variant = false;

        // Longest and most-specific phrases first.
        const std::string variant_phrases[] =
        {
            "maitreya petite x",
            "maitreya petitex",
            "maitreya lara x",
            "maitreya larax",
            "legacy athletic",
            "legacy perky",
            "legacy female",
            "legacy male",
            "belleza freya",
            "belleza isis",
            "belleza venus",
            "belleza jake",
            "signature gianni",
            "signature geralt",
            "signature davis",
            "inithium kupra",
            "kupra kups",
            "ebody reborn",
            "e-body reborn",
            "maitreya",
            "larax",
            "petitex",
            "legacy",
            "perky",
            "reborn",
            "waifu",
            "kupra",
            "kups",
            "belleza",
            "gianni",
            "geralt",
            "davis",
            "erika",
            "prima",
            "peach",
            "demo",
            "fatpack"
        };

        for (const std::string& phrase : variant_phrases)
        {
            if (stripCreatorProductVariantPhrase(family, phrase))
            {
                removed_variant = true;
            }
        }

        if (!removed_variant)
        {
            return std::string();
        }

        family = cleanCreatorProductFamilyLabel(family);

        if (family.size() < 2)
        {
            return std::string();
        }

        std::string original_key =
            cleanCreatorProductFamilyLabel(original);

        LLStringUtil::toLower(original_key);

        std::string family_key = family;
        LLStringUtil::toLower(family_key);

        if (family_key == original_key)
        {
            return std::string();
        }

        return family;
    }

    std::string creatorSubtypeLabelForItem(
        const LLViewerInventoryItem* item,
        S32 group) const
    {
        if (!item)
        {
            return "Other";
        }

        const std::string text = normalizedItemName(item);

        switch (group)
        {
            case CREATOR_GROUP_SHIRTS_TOPS:
                if (containsAny(text, {" t-shirt ", " t shirt ", " tshirt ", " tee ", " tees "}))
                    return "T-Shirts";
                if (containsAny(text, {" crop top ", " cropped top "}))
                    return "Crop Tops";
                if (containsAny(text, {" tank ", " tank top ", " tanktop "}))
                    return "Tanks";
                if (containsAny(text, {" sweater ", " sweatshirt "}))
                    return "Sweaters";
                if (containsAny(text, {" blouse ", " polo ", " tunic "}))
                    return "Blouses / Tunics";
                return "Other Tops";

            case CREATOR_GROUP_PANTS_BOTTOMS:
                if (containsAny(text, {" jeans ", " jean "}))
                    return "Jeans";
                if (containsAny(text, {" shorts ", " short "}))
                    return "Shorts";
                if (containsAny(text, {" leggings ", " legging "}))
                    return "Leggings";
                if (containsAny(text, {" joggers ", " jogger ", " sweatpants "}))
                    return "Joggers";
                if (containsAny(text, {" trousers ", " trouser ", " slacks "}))
                    return "Trousers";
                return "Other Bottoms";

            case CREATOR_GROUP_SHOES_FOOTWEAR:
                if (containsAny(text, {" boots ", " boot ", " booties ", " bootie "}))
                    return "Boots";
                if (containsAny(text, {" heels ", " heel ", " pumps ", " pump ", " stilettos ", " stiletto "}))
                    return "Heels / Pumps";
                if (containsAny(text, {" sneakers ", " sneaker ", " trainers ", " trainer "}))
                    return "Sneakers";
                if (containsAny(text, {" sandals ", " sandal ", " flip flops ", " flip-flops "}))
                    return "Sandals";
                if (containsAny(text, {" loafers ", " loafer ", " flats ", " flat "}))
                    return "Flats / Loafers";
                if (containsAny(text, {" slippers ", " slipper "}))
                    return "Slippers";
                return "Other Footwear";

            case CREATOR_GROUP_JACKETS_OUTERWEAR:
                if (containsAny(text, {" hoodie ", " hoodies "}))
                    return "Hoodies";
                if (containsAny(text, {" coat ", " coats ", " overcoat ", " trench ", " parka ", " parkas "}))
                    return "Coats";
                if (containsAny(text, {" blazer ", " blazers "}))
                    return "Blazers";
                if (containsAny(text, {" cardigan ", " cardigans "}))
                    return "Cardigans";
                if (containsAny(text, {" jacket ", " jackets ", " bolero "}))
                    return "Jackets";
                return "Other Outerwear";

            case CREATOR_GROUP_SOCKS_HOSIERY:
                if (containsAny(text, {" stockings ", " stocking ", " thigh highs ", " thigh-highs "}))
                    return "Stockings";
                if (containsAny(text, {" tights ", " hosiery "}))
                    return "Tights / Hosiery";
                if (containsAny(text, {" socks ", " sock "}))
                    return "Socks";
                return "Other Hosiery";

            case CREATOR_GROUP_GLOVES:
                if (containsAny(text, {" gauntlets ", " gauntlet "}))
                    return "Gauntlets";
                if (containsAny(text, {" mittens ", " mitten "}))
                    return "Mittens";
                if (containsAny(text, {" gloves ", " glove "}))
                    return "Gloves";
                return "Other Gloves";

            case CREATOR_GROUP_SKIRTS:
                if (containsAny(text, {" miniskirt ", " mini skirt "}))
                    return "Mini Skirts";
                return "Skirts";

            case CREATOR_GROUP_DRESSES_OUTFITS:
                if (containsAny(text, {" gown ", " gowns "}))
                    return "Gowns";
                if (containsAny(text, {" bodysuit ", " bodysuits ", " catsuit ", " catsuits "}))
                    return "Bodysuits";
                if (containsAny(text, {" jumpsuit ", " jumpsuits ", " romper ", " rompers "}))
                    return "Jumpsuits / Rompers";
                if (containsAny(text, {" dress ", " dresses "}))
                    return "Dresses";
                return "Other Outfits";

            case CREATOR_GROUP_UNDERWEAR:
                if (containsAny(text, {" bra ", " bras "}))
                    return "Bras";
                if (containsAny(text, {" panties ", " panty ", " thong ", " thongs "}))
                    return "Panties / Thongs";
                if (containsAny(text, {" boxers ", " boxer ", " briefs ", " brief "}))
                    return "Boxers / Briefs";
                if (containsAny(text, {" lingerie ", " lingerie set "}))
                    return "Lingerie";
                return "Other Underwear";

            case CREATOR_GROUP_HAIR:
                if (containsAny(text, {" ponytail ", " ponytails "}))
                    return "Ponytails";
                if (containsAny(text, {" braid ", " braids ", " braided "}))
                    return "Braids";
                if (containsAny(text, {" bun ", " buns "}))
                    return "Buns";
                if (containsAny(text, {" bangs ", " bang "}))
                    return "Bangs";
                if (containsAny(text, {" wig ", " wigs "}))
                    return "Wigs";
                return "Other Hair";

            case CREATOR_GROUP_BODY_PARTS:
                if (containsAny(text, {" head ", " heads ", " mesh head "}))
                    return "Heads";
                if (containsAny(text, {" body ", " bodies ", " mesh body "}))
                    return "Bodies";
                if (containsAny(text, {" eyes ", " eye "}))
                    return "Eyes";
                if (containsAny(text, {" hands ", " hand "}))
                    return "Hands";
                if (containsAny(text, {" feet ", " foot "}))
                    return "Feet";
                if (containsAny(text, {" shape ", " shapes "}))
                    return "Shapes";
                if (containsAny(text, {" skin ", " skins "}))
                    return "Skins";
                return "Other Body Parts";

            case CREATOR_GROUP_BODY_LAYERS:
                if (containsAny(text, {" tattoo ", " tattoos "}))
                    return "Tattoos";
                if (containsAny(text, {" alpha ", " alpha layer "}))
                    return "Alpha Layers";
                if (containsAny(text, {" bom ", " bom layer ", " bake on mesh ", " bakes on mesh "}))
                    return "BOM Layers";
                if (containsAny(text, {" universal ", " universal layer "}))
                    return "Universal Layers";
                if (containsAny(text, {" physics ", " physics layer "}))
                    return "Physics";
                return "Other Body Layers";

            case CREATOR_GROUP_ACCESSORIES:
                if (containsAny(text, {" hud ", " controller ", " applier "}))
                    return "HUDs / Controllers";
                if (containsAny(text, {" necklace ", " necklaces ", " earring ", " earrings ", " bracelet ", " bracelets ", " ring ", " rings ", " choker ", " chokers ", " watch ", " watches ", " piercing ", " piercings "}))
                    return "Jewelry";
                if (containsAny(text, {" glasses ", " sunglasses ", " eyewear "}))
                    return "Eyewear";
                if (containsAny(text, {" bag ", " bags ", " purse ", " purses ", " backpack ", " backpacks "}))
                    return "Bags";
                if (containsAny(text, {" hat ", " hats ", " cap ", " caps ", " beanie ", " crown ", " crowns "}))
                    return "Headwear";
                if (containsAny(text, {" mask ", " masks "}))
                    return "Masks";
                if (containsAny(text, {" belt ", " belts ", " harness ", " harnesses "}))
                    return "Belts / Harnesses";
                if (containsAny(text, {" nails ", " nail "}))
                    return "Nails";
                if (containsAny(text, {" horns ", " horn ", " wings ", " wing ", " tail ", " tails "}))
                    return "Fantasy Parts";
                return "Other Accessories";

            case CREATOR_GROUP_OTHER_WEARABLES:
            default:
                return "Miscellaneous";
        }
    }

    S32 automaticGroupForItem(
        const LLViewerInventoryItem* item) const
    {
        if (!item)
        {
            return -1;
        }

        if (item->getType() == LLAssetType::AT_CLOTHING
            || item->getType() == LLAssetType::AT_BODYPART)
        {
            return groupFromWearableType(item->getWearableType());
        }

        if (item->getType() != LLAssetType::AT_OBJECT)
        {
            return -1;
        }

        const std::string item_name = normalizedItemName(item);

        S32 group = groupFromObjectText(item_name, true);
        if (group >= 0)
        {
            return group;
        }

        const std::string parent_path = normalizedParentPath(item);

        group = groupFromObjectText(parent_path, false);
        if (group >= 0)
        {
            return group;
        }

        if (item->getInventoryType() == LLInventoryType::IT_ATTACHMENT)
        {
            return CREATOR_GROUP_OTHER_WEARABLES;
        }

        return -1;
    }

    S32 creatorGroupForItem(
        const LLViewerInventoryItem* item) const
    {
        if (!item)
        {
            return -1;
        }

        auto override_it =
            mClassificationOverrides.find(item->getUUID());

        if (override_it != mClassificationOverrides.end())
        {
            return override_it->second;
        }

        return automaticGroupForItem(item);
    }

    S32 groupFromClassificationAction(
        const std::string& action) const
    {
        if (action == "creator_classify_shirts")
            return CREATOR_GROUP_SHIRTS_TOPS;
        if (action == "creator_classify_pants")
            return CREATOR_GROUP_PANTS_BOTTOMS;
        if (action == "creator_classify_shoes")
            return CREATOR_GROUP_SHOES_FOOTWEAR;
        if (action == "creator_classify_jackets")
            return CREATOR_GROUP_JACKETS_OUTERWEAR;
        if (action == "creator_classify_socks")
            return CREATOR_GROUP_SOCKS_HOSIERY;
        if (action == "creator_classify_gloves")
            return CREATOR_GROUP_GLOVES;
        if (action == "creator_classify_skirts")
            return CREATOR_GROUP_SKIRTS;
        if (action == "creator_classify_dresses")
            return CREATOR_GROUP_DRESSES_OUTFITS;
        if (action == "creator_classify_underwear")
            return CREATOR_GROUP_UNDERWEAR;
        if (action == "creator_classify_hair")
            return CREATOR_GROUP_HAIR;
        if (action == "creator_classify_body_parts")
            return CREATOR_GROUP_BODY_PARTS;
        if (action == "creator_classify_body_layers")
            return CREATOR_GROUP_BODY_LAYERS;
        if (action == "creator_classify_accessories")
            return CREATOR_GROUP_ACCESSORIES;
        if (action == "creator_classify_other")
            return CREATOR_GROUP_OTHER_WEARABLES;

        return -1;
    }

    std::string creatorGroupLabel(S32 group) const
    {
        switch (group)
        {
            case CREATOR_GROUP_SHIRTS_TOPS:
                return "Shirts / Tops";
            case CREATOR_GROUP_PANTS_BOTTOMS:
                return "Pants / Bottoms";
            case CREATOR_GROUP_SHOES_FOOTWEAR:
                return "Shoes / Footwear";
            case CREATOR_GROUP_JACKETS_OUTERWEAR:
                return "Jackets / Outerwear";
            case CREATOR_GROUP_SOCKS_HOSIERY:
                return "Socks / Hosiery";
            case CREATOR_GROUP_GLOVES:
                return "Gloves";
            case CREATOR_GROUP_SKIRTS:
                return "Skirts";
            case CREATOR_GROUP_DRESSES_OUTFITS:
                return "Dresses / Full Outfits";
            case CREATOR_GROUP_UNDERWEAR:
                return "Underwear / Lingerie";
            case CREATOR_GROUP_HAIR:
                return "Hair";
            case CREATOR_GROUP_BODY_PARTS:
                return "Body Parts";
            case CREATOR_GROUP_BODY_LAYERS:
                return "Body Layers";
            case CREATOR_GROUP_ACCESSORIES:
                return "Accessories";
            case CREATOR_GROUP_OTHER_WEARABLES:
                return "Other Wearables";
            default:
                return "Other Wearables";
        }
    }

    bool shouldInclude(const LLInventoryObject* object)
    {
        const LLViewerInventoryItem* item =
            dynamic_cast<const LLViewerInventoryItem*>(object);

        if (!item || item->getIsLinkType())
        {
            return false;
        }

        if (creatorGroupForItem(item) < 0)
        {
            return false;
        }

        const LLUUID trash_id =
            mInventory->findCategoryUUIDForType(LLFolderType::FT_TRASH);

        if (trash_id.notNull()
            && (item->getParentUUID() == trash_id
                || mInventory->isObjectDescendentOf(
                    item->getUUID(), trash_id)))
        {
            return false;
        }

        const LLUUID marketplace_id =
            mInventory->getMarketplaceListingsUUID();

        if (marketplace_id.notNull()
            && (item->getParentUUID() == marketplace_id
                || mInventory->isObjectDescendentOf(
                    item->getUUID(), marketplace_id)))
        {
            return false;
        }

        return typedViewsFilter(item->getUUID(), item);
    }

    void loadClassificationOverrides()
    {
        mClassificationOverrides.clear();

        const std::string saved =
            gSavedSettings.getString("CreatorViewItemClassifications");

        std::string::size_type start = 0;

        while (start < saved.size())
        {
            const std::string::size_type end =
                saved.find('\n', start);

            const std::string line =
                saved.substr(
                    start,
                    (end == std::string::npos)
                        ? std::string::npos
                        : end - start);

            const std::string::size_type split = line.find('|');

            if (split != std::string::npos)
            {
                const std::string id_text = line.substr(0, split);
                const std::string group_text = line.substr(split + 1);

                LLUUID item_id(id_text);

                if (item_id.notNull())
                {
                    const S32 group = atoi(group_text.c_str());

                    if (group >= CREATOR_GROUP_SHIRTS_TOPS
                        && group <= CREATOR_GROUP_OTHER_WEARABLES)
                    {
                        mClassificationOverrides[item_id] = group;
                    }
                }
            }

            if (end == std::string::npos)
            {
                break;
            }

            start = end + 1;
        }
    }

    void saveClassificationOverrides() const
    {
        std::string saved;
        bool first = true;

        for (const auto& entry : mClassificationOverrides)
        {
            if (!first)
            {
                saved += "\n";
            }

            saved += entry.first.asString();
            saved += "|";
            saved += std::to_string(entry.second);
            first = false;
        }

        gSavedSettings.setString(
            "CreatorViewItemClassifications",
            saved);
    }

    void applyClassificationAction(const std::string& action)
    {
        if (!mFolderRoot.get())
        {
            return;
        }

        const bool use_auto =
            (action == "creator_classify_auto");

        const S32 requested_group =
            use_auto ? -1 : groupFromClassificationAction(action);

        if (!use_auto && requested_group < 0)
        {
            return;
        }

        std::vector<LLUUID> selected_ids;

        const std::set<LLFolderViewItem*> selection =
            mFolderRoot.get()->getSelectionList();

        for (LLFolderViewItem* selected : selection)
        {
            if (!selected || !selected->getViewModelItem())
            {
                continue;
            }

            LLFolderViewModelItemInventory* vm_item =
                dynamic_cast<LLFolderViewModelItemInventory*>(
                    selected->getViewModelItem());

            if (!vm_item)
            {
                continue;
            }

            const LLUUID item_id = vm_item->getUUID();

            if (gInventory.getItem(item_id))
            {
                selected_ids.push_back(item_id);
            }
        }

        for (const LLUUID& item_id : selected_ids)
        {
            LLViewerInventoryItem* item =
                gInventory.getItem(item_id);

            if (!item)
            {
                continue;
            }

            if (use_auto)
            {
                mClassificationOverrides.erase(item_id);
            }
            else
            {
                mClassificationOverrides[item_id] = requested_group;
            }

            LLFolderViewItem* view_item =
                getItemByID(item_id);

            const S32 resolved_group =
                creatorGroupForItem(item);

            if (resolved_group < 0)
            {
                if (view_item)
                {
                    removeItemID(item_id);
                    view_item->destroyView();
                }
                continue;
            }

            const LLUUID creator_id =
                item->getPermissions().getCreator();

            LLFolderViewFolder* creator_folder =
                ensureCreatorFolder(creator_id);

            LLFolderViewFolder* type_folder =
                ensureTypeFolder(
                    creator_id,
                    resolved_group,
                    creator_folder);

            if (!type_folder)
            {
                continue;
            }

            const std::string subtype_label =
                creatorSubtypeLabelForItem(item, resolved_group);

            LLFolderViewFolder* subtype_folder =
                ensureSubtypeFolder(
                    creator_id,
                    resolved_group,
                    subtype_label,
                    type_folder);

            if (!subtype_folder)
            {
                continue;
            }

            LLFolderViewFolder* item_parent =
                productFamilyParentForItem(
                    creator_id,
                    resolved_group,
                    subtype_label,
                    item,
                    subtype_folder);

            if (!item_parent)
            {
                continue;
            }

            if (!view_item)
            {
                buildViewsTree(
                    item_id,
                    item->getParentUUID(),
                    item,
                    nullptr,
                    item_parent,
                    BUILD_ONE_FOLDER);

                view_item = getItemByID(item_id);
            }

            if (view_item)
            {
                if (view_item->getParentFolder() != item_parent)
                {
                    view_item->addToFolder(item_parent);
                }

                view_item->refresh();

                if (view_item->getViewModelItem())
                {
                    view_item->getViewModelItem()->dirtyFilter();
                    view_item->getViewModelItem()->requestSort();
                }

                item_parent->requestArrange();
                subtype_folder->requestArrange();
                type_folder->requestArrange();

                if (creator_folder)
                {
                    creator_folder->requestArrange();
                }
            }
        }

        saveClassificationOverrides();

        if (mFolderRoot.get())
        {
            mFolderRoot.get()->requestArrange();
        }
    }

    LLUUID groupIDForCreator(const LLUUID& creator_id) const
    {
        if (creator_id.notNull())
        {
            return creator_id;
        }

        static const LLUUID unknown_creator_group_id(
            "f3bca0c1-6f3d-4da4-9dc0-5a4130c7e901");

        return unknown_creator_group_id;
    }

    std::string typeStateKey(
        const LLUUID& creator_id,
        S32 group) const
    {
        return creator_id.asString()
            + "|"
            + std::to_string(group);
    }

    std::string subtypeStateKey(
        const LLUUID& creator_id,
        S32 group,
        const std::string& subtype) const
    {
        return typeStateKey(creator_id, group)
            + "|"
            + subtype;
    }

    std::string productFamilyStateKey(
        const LLUUID& creator_id,
        S32 group,
        const std::string& subtype,
        const std::string& family_key) const
    {
        return subtypeStateKey(
            creator_id,
            group,
            subtype)
            + "|product|"
            + family_key;
    }

    void loadTypeOpenState()
    {
        mOpenTypeFolderKeys.clear();

        const std::string saved =
            gSavedSettings.getString("CreatorViewOpenTypeFolders");

        std::string::size_type start = 0;

        while (start < saved.size())
        {
            const std::string::size_type end =
                saved.find('\n', start);

            const std::string key =
                saved.substr(
                    start,
                    (end == std::string::npos)
                        ? std::string::npos
                        : end - start);

            if (!key.empty())
            {
                mOpenTypeFolderKeys.insert(key);
            }

            if (end == std::string::npos)
            {
                break;
            }

            start = end + 1;
        }
    }

    void saveTypeOpenState() const
    {
        std::string saved;
        bool first = true;

        for (const std::string& key : mOpenTypeFolderKeys)
        {
            if (!first)
            {
                saved += "\n";
            }

            saved += key;
            first = false;
        }

        gSavedSettings.setString(
            "CreatorViewOpenTypeFolders",
            saved);
    }
    void loadOpenState()
    {
        mOpenCreatorIDs.clear();

        const std::string saved =
            gSavedSettings.getString("CreatorViewOpenCreatorFolders");

        std::string::size_type start = 0;

        while (start < saved.size())
        {
            const std::string::size_type end =
                saved.find(';', start);

            const std::string token =
                saved.substr(
                    start,
                    (end == std::string::npos)
                        ? std::string::npos
                        : end - start);

            if (!token.empty())
            {
                LLUUID id(token);
                if (id.notNull())
                {
                    mOpenCreatorIDs.insert(id);
                }
            }

            if (end == std::string::npos)
            {
                break;
            }

            start = end + 1;
        }
    }

    void saveOpenState() const
    {
        std::string saved;
        bool first = true;

        for (const LLUUID& id : mOpenCreatorIDs)
        {
            if (!first)
            {
                saved += ";";
            }

            saved += id.asString();
            first = false;
        }

        gSavedSettings.setString(
            "CreatorViewOpenCreatorFolders",
            saved);
    }

    LLFolderViewFolder* ensureCreatorFolder(const LLUUID& creator_id)
    {
        auto found = mCreatorFolders.find(creator_id);
        if (found != mCreatorFolders.end() && found->second)
        {
            return found->second;
        }

        if (!mFolderRoot.get())
        {
            return nullptr;
        }

        const LLUUID group_id = groupIDForCreator(creator_id);

        if (LLFolderViewFolder* existing = getFolderByID(group_id))
        {
            mCreatorFolders[creator_id] = existing;
            return existing;
        }

        LLCreatorVirtualFolderBridge* bridge =
            new LLCreatorVirtualFolderBridge(
                this,
                mFolderRoot.get(),
                group_id,
                creator_id);

        LLFolderViewFolder* folder =
            createFolderViewFolder(bridge, false);

        if (!folder)
        {
            delete bridge;
            return nullptr;
        }

        folder->addToFolder(mFolderRoot.get());
        folder->setChildrenInited(true);

        const bool saved_open =
            (mOpenCreatorIDs.find(group_id) != mOpenCreatorIDs.end());

        folder->setOpen(saved_open);

        addItemID(group_id, folder);
        mCreatorFolders[creator_id] = folder;

        return folder;
    }

    LLFolderViewFolder* ensureTypeFolder(
        const LLUUID& creator_id,
        S32 group,
        LLFolderViewFolder* creator_folder)
    {
        if (!creator_folder || group < 0)
        {
            return nullptr;
        }

        auto creator_types = mTypeFolders.find(creator_id);
        if (creator_types != mTypeFolders.end())
        {
            auto found = creator_types->second.find(group);
            if (found != creator_types->second.end() && found->second)
            {
                return found->second;
            }
        }

        const LLUUID type_group_id = LLUUID::generateNewID();

        LLCreatorTypeVirtualFolderBridge* bridge =
            new LLCreatorTypeVirtualFolderBridge(
                this,
                mFolderRoot.get(),
                type_group_id,
                creatorGroupLabel(group));

        LLFolderViewFolder* folder =
            createFolderViewFolder(bridge, false);

        if (!folder)
        {
            delete bridge;
            return nullptr;
        }

        folder->addToFolder(creator_folder);
        folder->setChildrenInited(true);
        const std::string type_state_key =
            typeStateKey(creator_id, group);

        const bool saved_open =
            (mOpenTypeFolderKeys.find(type_state_key)
                != mOpenTypeFolderKeys.end());

        folder->setOpen(saved_open);

        addItemID(type_group_id, folder);
        mTypeFolders[creator_id][group] = folder;

        return folder;
    }

    LLFolderViewFolder* ensureSubtypeFolder(
        const LLUUID& creator_id,
        S32 group,
        const std::string& subtype,
        LLFolderViewFolder* type_folder)
    {
        if (!type_folder || group < 0 || subtype.empty())
        {
            return nullptr;
        }

        auto creator_subtypes = mSubtypeFolders.find(creator_id);
        if (creator_subtypes != mSubtypeFolders.end())
        {
            auto group_subtypes = creator_subtypes->second.find(group);
            if (group_subtypes != creator_subtypes->second.end())
            {
                auto found = group_subtypes->second.find(subtype);
                if (found != group_subtypes->second.end() && found->second)
                {
                    return found->second;
                }
            }
        }

        const LLUUID subtype_group_id = LLUUID::generateNewID();

        LLCreatorTypeVirtualFolderBridge* bridge =
            new LLCreatorTypeVirtualFolderBridge(
                this,
                mFolderRoot.get(),
                subtype_group_id,
                subtype);

        LLFolderViewFolder* folder =
            createFolderViewFolder(bridge, false);

        if (!folder)
        {
            delete bridge;
            return nullptr;
        }

        folder->addToFolder(type_folder);
        folder->setChildrenInited(true);

        const std::string subtype_state_key =
            subtypeStateKey(creator_id, group, subtype);

        const bool saved_open =
            (mOpenTypeFolderKeys.find(subtype_state_key)
                != mOpenTypeFolderKeys.end());

        folder->setOpen(saved_open);

        addItemID(subtype_group_id, folder);
        mSubtypeFolders[creator_id][group][subtype] = folder;

        return folder;
    }

    LLFolderViewFolder* ensureProductFamilyFolder(
        const LLUUID& creator_id,
        S32 group,
        const std::string& subtype,
        const std::string& family_label,
        LLFolderViewFolder* subtype_folder)
    {
        if (!subtype_folder
            || group < 0
            || subtype.empty()
            || family_label.empty())
        {
            return nullptr;
        }

        const std::string family_key =
            creatorProductFamilyKey(family_label);

        if (family_key.empty())
        {
            return nullptr;
        }

        auto creator_products =
            mProductFamilyFolders.find(creator_id);

        if (creator_products != mProductFamilyFolders.end())
        {
            auto group_products =
                creator_products->second.find(group);

            if (group_products != creator_products->second.end())
            {
                auto subtype_products =
                    group_products->second.find(subtype);

                if (subtype_products != group_products->second.end())
                {
                    auto found =
                        subtype_products->second.find(family_key);

                    if (found != subtype_products->second.end()
                        && found->second)
                    {
                        return found->second;
                    }
                }
            }
        }

        const LLUUID product_group_id =
            LLUUID::generateNewID();

        LLCreatorTypeVirtualFolderBridge* bridge =
            new LLCreatorTypeVirtualFolderBridge(
                this,
                mFolderRoot.get(),
                product_group_id,
                family_label);

        LLFolderViewFolder* folder =
            createFolderViewFolder(bridge, false);

        if (!folder)
        {
            delete bridge;
            return nullptr;
        }

        folder->addToFolder(subtype_folder);
        folder->setChildrenInited(true);

        const std::string product_state_key =
            productFamilyStateKey(
                creator_id,
                group,
                subtype,
                family_key);

        const bool saved_open =
            (mOpenTypeFolderKeys.find(product_state_key)
                != mOpenTypeFolderKeys.end());

        folder->setOpen(saved_open);

        addItemID(product_group_id, folder);

        mProductFamilyFolders[creator_id]
            [group]
            [subtype]
            [family_key] = folder;

        return folder;
    }

    LLFolderViewFolder* productFamilyParentForItem(
        const LLUUID& creator_id,
        S32 group,
        const std::string& subtype,
        const LLViewerInventoryItem* item,
        LLFolderViewFolder* subtype_folder)
    {
        if (!subtype_folder || !item)
        {
            return subtype_folder;
        }

        const std::string family_label =
            creatorProductFamilyLabelForItem(item);

        if (family_label.empty())
        {
            return subtype_folder;
        }

        LLFolderViewFolder* product_folder =
            ensureProductFamilyFolder(
                creator_id,
                group,
                subtype,
                family_label,
                subtype_folder);

        return product_folder
            ? product_folder
            : subtype_folder;
    }

    void findAndInitRootContent(const LLUUID& id) override
    {
        const F64 current_time = LLTimer::getTotalSeconds();

        if (mBuildViewsEndTime < current_time)
        {
            mBuildRootQueue.emplace_back(id);
            return;
        }

        LLViewerInventoryCategory::cat_array_t* categories = nullptr;
        LLViewerInventoryItem::item_array_t* items = nullptr;

        mInventory->lockDirectDescendentArrays(id, categories, items);

        if (categories)
        {
            const LLUUID trash_id =
                mInventory->findCategoryUUIDForType(LLFolderType::FT_TRASH);
            const LLUUID marketplace_id =
                mInventory->getMarketplaceListingsUUID();

            for (LLViewerInventoryCategory* cat : *categories)
            {
                if (!cat)
                {
                    continue;
                }

                const LLUUID cat_id = cat->getUUID();

                if (cat_id == trash_id
                    || cat_id == marketplace_id
                    || cat->getPreferredType() == LLFolderType::FT_TRASH)
                {
                    continue;
                }

                findAndInitRootContent(cat_id);
            }
        }

        if (items)
        {
            for (LLViewerInventoryItem* item : *items)
            {
                if (!shouldInclude(item))
                {
                    continue;
                }

                const LLUUID item_id = item->getUUID();

                if (getItemByID(item_id))
                {
                    continue;
                }

                const LLUUID creator_id =
                    item->getPermissions().getCreator();

                const S32 item_group =
                    creatorGroupForItem(item);

                LLFolderViewFolder* creator_folder =
                    ensureCreatorFolder(creator_id);

                LLFolderViewFolder* type_folder =
                    ensureTypeFolder(
                        creator_id,
                        item_group,
                        creator_folder);

                if (!type_folder)
                {
                    continue;
                }

                const std::string subtype_label =
                    creatorSubtypeLabelForItem(item, item_group);

                LLFolderViewFolder* subtype_folder =
                    ensureSubtypeFolder(
                        creator_id,
                        item_group,
                        subtype_label,
                        type_folder);

                if (!subtype_folder)
                {
                    continue;
                }

                LLFolderViewFolder* item_parent =
                    productFamilyParentForItem(
                        creator_id,
                        item_group,
                        subtype_label,
                        item,
                        subtype_folder);

                if (!item_parent)
                {
                    continue;
                }

                buildViewsTree(
                    item_id,
                    item->getParentUUID(),
                    item,
                    nullptr,
                    item_parent,
                    BUILD_TIMELIMIT);
            }
        }

        mInventory->unlockDirectDescendentArrays(id);
    }

    void initRootContent() override
    {
        findAndInitRootContent(gInventory.getRootFolderID());
    }

    void itemChanged(
        const LLUUID& id,
        U32,
        const LLInventoryObject* model_item) override
    {
        LLFolderViewItem* view_item = getItemByID(id);

        if (!model_item)
        {
            if (view_item)
            {
                removeItemID(id);
                view_item->destroyView();
            }
            return;
        }

        const LLViewerInventoryItem* item =
            dynamic_cast<const LLViewerInventoryItem*>(model_item);

        if (!item)
        {
            return;
        }

        if (!shouldInclude(model_item))
        {
            if (view_item)
            {
                removeItemID(id);
                view_item->destroyView();
            }
            return;
        }

        const LLUUID creator_id =
            item->getPermissions().getCreator();

        const S32 item_group =
            creatorGroupForItem(item);

        LLFolderViewFolder* creator_folder =
            ensureCreatorFolder(creator_id);

        LLFolderViewFolder* type_folder =
            ensureTypeFolder(
                creator_id,
                item_group,
                creator_folder);

        if (!type_folder)
        {
            return;
        }

        const std::string subtype_label =
            creatorSubtypeLabelForItem(item, item_group);

        LLFolderViewFolder* subtype_folder =
            ensureSubtypeFolder(
                creator_id,
                item_group,
                subtype_label,
                type_folder);

        if (!subtype_folder)
        {
            return;
        }

        LLFolderViewFolder* item_parent =
            productFamilyParentForItem(
                creator_id,
                item_group,
                subtype_label,
                item,
                subtype_folder);

        if (!item_parent)
        {
            return;
        }

        if (!view_item)
        {
            buildViewsTree(
                id,
                item->getParentUUID(),
                model_item,
                nullptr,
                item_parent,
                BUILD_ONE_FOLDER);

            view_item = getItemByID(id);
        }

        if (view_item)
        {
            if (view_item->getParentFolder() != item_parent)
            {
                view_item->addToFolder(item_parent);
            }

            view_item->refresh();

            if (LLFolderViewModelItemInventory* vm_item =
                    static_cast<LLFolderViewModelItemInventory*>(
                        view_item->getViewModelItem()))
            {
                vm_item->requestSort();
            }

            if (item_parent->getViewModelItem())
            {
                item_parent->getViewModelItem()->requestSort();
            }

            if (subtype_folder->getViewModelItem())
            {
                subtype_folder->getViewModelItem()->requestSort();
            }

            if (type_folder->getViewModelItem())
            {
                type_folder->getViewModelItem()->requestSort();
            }

            if (creator_folder->getViewModelItem())
            {
                creator_folder->getViewModelItem()->requestSort();
            }

            item_parent->requestArrange();
            subtype_folder->requestArrange();
            type_folder->requestArrange();
            creator_folder->requestArrange();
        }
    }

private:
    std::map<LLUUID, LLFolderViewFolder*> mCreatorFolders;
    std::map<LLUUID, std::map<S32, LLFolderViewFolder*> > mTypeFolders;
    std::map<
        LLUUID,
        std::map<S32, std::map<std::string, LLFolderViewFolder*> > >
        mSubtypeFolders;
    std::map<
        LLUUID,
        std::map<
            S32,
            std::map<
                std::string,
                std::map<std::string, LLFolderViewFolder*> > > >
        mProductFamilyFolders;
    std::set<LLUUID> mOpenCreatorIDs;
    std::set<std::string> mOpenTypeFolderKeys;
    std::map<LLUUID, S32> mClassificationOverrides;
};
/************************************************************************/
/* Favorites Inventory Panel related class                              */
/************************************************************************/
static const LLFavoritesInventoryBridgeBuilder FAVORITES_BUILDER;
class LLInventoryFavoritesItemsPanel : public LLInventoryPanel
{
public:
    struct Params : public LLInitParam::Block<Params, LLInventoryPanel::Params>
    {};

    void initFromParams(const Params& p)
    {
        LLInventoryPanel::initFromParams(p);
        // turn off trash
        getFilter().setFilterCategoryTypes(getFilter().getFilterCategoryTypes() | (1ULL << LLFolderType::FT_TRASH));
        getFilter().setFilterNoTrashFolder();
        // turn off marketplace for favorites
        getFilter().setFilterNoMarketplaceFolder();
    }

    void removeItemID(const LLUUID& id) override;
    bool isInRootContent(const LLUUID& id, LLFolderViewItem* view_item) override;
    bool hasPredecessorsInRootContent(const LLInventoryObject* model_item) const;

protected:
    LLInventoryFavoritesItemsPanel(const Params&);
    friend class LLUICtrlFactory;

    void findAndInitRootContent(const LLUUID& folder_id) override;
    void initRootContent() override;

    // removeFavorite removes item from root, does not readd favorited children if present
    bool removeFavorite(const LLUUID& id, const LLInventoryObject* model_item);
    void itemChanged(const LLUUID& item_id, U32 mask, const LLInventoryObject* model_item) override;

    std::set<LLUUID> mRootContentIDs;
};

LLInventoryFavoritesItemsPanel::LLInventoryFavoritesItemsPanel(const Params& params)
    : LLInventoryPanel(params)
{
    // replace bridge builder to have necessary View bridges.
    mInvFVBridgeBuilder = &FAVORITES_BUILDER;
}

void LLInventoryFavoritesItemsPanel::removeItemID(const LLUUID& id)
{
    std::set<LLUUID>::iterator found = mRootContentIDs.find(id);
    if (found != mRootContentIDs.end())
    {
        mRootContentIDs.erase(found);
        // check content for favorites
        mBuildRootQueue.emplace_back(id);
    }

    LLInventoryPanel::removeItemID(id);
}

bool LLInventoryFavoritesItemsPanel::isInRootContent(const LLUUID& id, LLFolderViewItem* view_item)
{
    if (!view_item->isFavorite())
    {
        return false;
    }

    std::set<LLUUID>::iterator found = mRootContentIDs.find(id);
    return found != mRootContentIDs.end();
}

bool LLInventoryFavoritesItemsPanel::hasPredecessorsInRootContent(const LLInventoryObject* obj) const
{
    LLUUID parent_id = obj->getParentUUID();
    while (parent_id.notNull())
    {
        if (mRootContentIDs.contains(parent_id))
        {
            return true;
        }
        LLViewerInventoryCategory* cat = mInventory->getCategory(parent_id);
        if (cat)
        {
            parent_id = cat->getParentUUID();
        }
    }
    return false;
}

void LLInventoryFavoritesItemsPanel::findAndInitRootContent(const LLUUID& id)
{
    F64 curent_time = LLTimer::getTotalSeconds();
    if (mBuildViewsEndTime < curent_time)
    {
        mBuildRootQueue.emplace_back(id);
        return;
    }
    LLViewerInventoryCategory::cat_array_t* categories;
    LLViewerInventoryItem::item_array_t* items;
    mInventory->lockDirectDescendentArrays(id, categories, items);

    if (categories)
    {
        S32 count = static_cast<S32>(categories->size());
        for (S32 i = 0; i < count; ++i)
        {
            LLViewerInventoryCategory* cat = categories->at(i);
            if (cat->getPreferredType() == LLFolderType::FT_TRASH)
            {
                continue;
            }
            else if (cat->getIsFavorite())
            {
                LLFolderViewItem* folder_view_item = getItemByID(cat->getUUID());
                if (!folder_view_item)
                {
                    const LLUUID& parent_id = cat->getParentUUID();
                    mRootContentIDs.emplace(cat->getUUID());

                    buildViewsTree(cat->getUUID(), parent_id, cat, folder_view_item, mFolderRoot.get(), BUILD_TIMELIMIT);
                }
            }
            else
            {
                findAndInitRootContent(cat->getUUID());
            }
        }
    }

    if (items)
    {
        S32 count = static_cast<S32>(items->size());
        for (S32 i = 0; i < count; ++i)
        {
            LLViewerInventoryItem* item = items->at(i);
            const LLUUID item_id = item->getUUID();
            if (item->getIsFavorite() && typedViewsFilter(item_id, item))
            {
                LLFolderViewItem* folder_view_item = getItemByID(id);
                if (!folder_view_item)
                {
                    const LLUUID& parent_id = item->getParentUUID();
                    mRootContentIDs.emplace(item_id);

                    buildViewsTree(item_id, parent_id, item, folder_view_item, mFolderRoot.get(), BUILD_TIMELIMIT);
                }
            }
        }
    }

    mInventory->unlockDirectDescendentArrays(id);
}

void LLInventoryFavoritesItemsPanel::initRootContent()
{
    findAndInitRootContent(gInventory.getRootFolderID()); // My Inventory
}

bool LLInventoryFavoritesItemsPanel::removeFavorite(const LLUUID& id, const LLInventoryObject* model_item)
{
    std::set<LLUUID>::iterator found = mRootContentIDs.find(id);
    if (found == mRootContentIDs.end())
    {
        return false;
    }

    mRootContentIDs.erase(found);

    // This item is in root's content, remove item's UI.
    LLFolderViewItem* view_item = getItemByID(id);
    if (view_item)
    {
        LLFolderViewFolder* parent = view_item->getParentFolder();
        LLFolderViewModelItemInventory* viewmodel_item = static_cast<LLFolderViewModelItemInventory*>(view_item->getViewModelItem());
        if (viewmodel_item)
        {
            removeItemID(viewmodel_item->getUUID());
        }
        bool was_favorite = view_item->isFavorite();
        view_item->destroyView();
        if (parent)
        {
            parent->getViewModelItem()->dirtyDescendantsFilter();
            LLFolderViewModelItemInventory* viewmodel_folder = static_cast<LLFolderViewModelItemInventory*>(parent->getViewModelItem());
            if (viewmodel_folder)
            {
                updateFolderLabel(viewmodel_folder->getUUID());
            }
            if (was_favorite)
            {
                parent->updateHasFavorites(false); // favorite was removed
            }
        }
    }

    return true;
}

void LLInventoryFavoritesItemsPanel::itemChanged(const LLUUID& id, U32 mask, const LLInventoryObject* model_item)
{
    LLFolderViewItem* view_item = getItemByID(id);
    if (!model_item && !view_item)
    {
        // remove operation, but item is not in panel already
        return;
    }

    bool handled = false;

    if (mask & (LLInventoryObserver::UPDATE_FAVORITE |
        LLInventoryObserver::STRUCTURE |
        LLInventoryObserver::ADD |
        LLInventoryObserver::REMOVE))
    {
        // specifically exlude links and not get_is_favorite(model_item)
        if (model_item && model_item->getIsFavorite())
        {
            if (!view_item)
            {
                const LLViewerInventoryCategory* cat = dynamic_cast<const LLViewerInventoryCategory*>(model_item);
                if (cat)
                {
                    // New favorite folder
                    if (cat->getPreferredType() != LLFolderType::FT_TRASH)
                    {
                        // If any descendants were in the list, remove them
                        // Todo: Consider implementing and checking hasFavorites to save on search
                        LLFavoritesCollector is_favorite;
                        LLInventoryModel::cat_array_t cat_array;
                        LLInventoryModel::item_array_t item_array;
                        gInventory.collectDescendentsIf(id, cat_array, item_array, false, is_favorite);
                        for (LLInventoryModel::cat_array_t::const_iterator it = cat_array.begin(); it != cat_array.end(); ++it)
                        {
                            removeFavorite((*it)->getUUID(), *it);
                        }
                        for (LLInventoryModel::item_array_t::const_iterator it = item_array.begin(); it != item_array.end(); ++it)
                        {
                            removeFavorite((*it)->getUUID(), *it);
                        }

                        LLFolderViewItem* folder_view_item = getItemByID(cat->getUUID());
                        if (!folder_view_item
                            && !hasPredecessorsInRootContent(model_item))
                        {
                            const LLUUID& parent_id = cat->getParentUUID();
                            mRootContentIDs.emplace(cat->getUUID());

                            buildViewsTree(cat->getUUID(), parent_id, cat, folder_view_item, mFolderRoot.get(), BUILD_ONE_FOLDER);
                        }
                    }
                }
                else
                {
                    // New favorite item
                    if (model_item->getIsFavorite()
                        && typedViewsFilter(id, model_item)
                        && !hasPredecessorsInRootContent(model_item))
                    {
                        const LLUUID& parent_id = model_item->getParentUUID();
                        mRootContentIDs.emplace(id);

                        buildViewsTree(id, parent_id, model_item, NULL, mFolderRoot.get(), BUILD_ONE_FOLDER);
                    }
                }
                handled = true;
            }
        }
        else
        {
            handled = removeFavorite(id, model_item);
            if (handled)
            {
                const LLViewerInventoryCategory* cat = dynamic_cast<const LLViewerInventoryCategory*>(model_item);
                // Todo: Consider implementing and checking hasFavorites to save on search
                if (cat)
                {
                    // re-add any favorited children
                    mBuildRootQueue.emplace_back(id);
                }
            }
        }
    }

    if (!handled
        && (!model_item || model_item->getParentUUID().notNull())) // filter out 'My inventory'
    {
        LLInventoryPanel::itemChanged(id, mask, model_item);
    }
}
/************************************************************************/
/* LLInventorySingleFolderPanel                                         */
/************************************************************************/

static LLDefaultChildRegistry::Register<LLInventorySingleFolderPanel> t_single_folder_inventory_panel("single_folder_inventory_panel");

LLInventorySingleFolderPanel::LLInventorySingleFolderPanel(const Params& params)
    : LLInventoryPanel(params)
{
    mBuildChildrenViews = false;
    getFilter().setSingleFolderMode(true);
    getFilter().setEmptyLookupMessage("InventorySingleFolderNoMatches");
    getFilter().setDefaultEmptyLookupMessage("InventorySingleFolderEmpty");

    mCommitCallbackRegistrar.replace("Inventory.DoToSelected", boost::bind(&LLInventorySingleFolderPanel::doToSelected, this, _2));
    mCommitCallbackRegistrar.replace("Inventory.DoCreate", boost::bind(&LLInventorySingleFolderPanel::doCreate, this, _2));
    mCommitCallbackRegistrar.replace("Inventory.Share", boost::bind(&LLInventorySingleFolderPanel::doShare, this));
}

LLInventorySingleFolderPanel::~LLInventorySingleFolderPanel()
{
}

void LLInventorySingleFolderPanel::initFromParams(const Params& p)
{
    mFolderID = gInventory.getRootFolderID();

    mParams = p;
    LLPanel::initFromParams(mParams);
}

void LLInventorySingleFolderPanel::onFocusReceived()
{
    // Tab support, when tabbing into this view, select first item
    // (ideally needs to account for scroll)
    bool select_first = mSelectThisID.isNull() && mFolderRoot.get() && mFolderRoot.get()->getSelectedCount() == 0;

    if (select_first)
    {
        LLFolderViewFolder::folders_t::const_iterator folders_it = mFolderRoot.get()->getFoldersBegin();
        LLFolderViewFolder::folders_t::const_iterator folders_end = mFolderRoot.get()->getFoldersEnd();

        for (; folders_it != folders_end; ++folders_it)
        {
            const LLFolderViewFolder* folder_view = *folders_it;
            if (folder_view->getVisible())
            {
                const LLFolderViewModelItemInventory* modelp = static_cast<const LLFolderViewModelItemInventory*>(folder_view->getViewModelItem());
                setSelectionByID(modelp->getUUID(), true);
                // quick and dirty fix: don't scroll on switching focus
                // todo: better 'tab' support, one that would work for LLInventoryPanel
                mFolderRoot.get()->stopAutoScollining();
                select_first = false;
                break;
            }
        }
    }

    if (select_first)
    {
        LLFolderViewFolder::items_t::const_iterator items_it = mFolderRoot.get()->getItemsBegin();
        LLFolderViewFolder::items_t::const_iterator items_end = mFolderRoot.get()->getItemsEnd();

        for (; items_it != items_end; ++items_it)
        {
            const LLFolderViewItem* item_view = *items_it;
            if (item_view->getVisible())
            {
                const LLFolderViewModelItemInventory* modelp = static_cast<const LLFolderViewModelItemInventory*>(item_view->getViewModelItem());
                setSelectionByID(modelp->getUUID(), true);
                mFolderRoot.get()->stopAutoScollining();
                break;
            }
        }
    }
    LLInventoryPanel::onFocusReceived();
}

void LLInventorySingleFolderPanel::initFolderRoot(const LLUUID& start_folder_id)
{
    if(mRootInited) return;

    mRootInited = true;
    if(start_folder_id.notNull())
    {
        mFolderID = start_folder_id;
    }

    mParams.open_first_folder = false;
    mParams.start_folder.id = mFolderID;

    LLInventoryPanel::initFolderRoot();
    mFolderRoot.get()->setSingleFolderMode(true);
}

void LLInventorySingleFolderPanel::changeFolderRoot(const LLUUID& new_id)
{
    if (mFolderID != new_id)
    {
        if(mFolderID.notNull())
        {
            mBackwardFolders.push_back(mFolderID);
        }
        mFolderID = new_id;
        updateSingleFolderRoot();
    }
}

void LLInventorySingleFolderPanel::onForwardFolder()
{
    if(isForwardAvailable())
    {
        mBackwardFolders.push_back(mFolderID);
        mFolderID = mForwardFolders.back();
        mForwardFolders.pop_back();
        updateSingleFolderRoot();
    }
}

void LLInventorySingleFolderPanel::onBackwardFolder()
{
    if(isBackwardAvailable())
    {
        mForwardFolders.push_back(mFolderID);
        mFolderID = mBackwardFolders.back();
        mBackwardFolders.pop_back();
        updateSingleFolderRoot();
    }
}

void LLInventorySingleFolderPanel::clearNavigationHistory()
{
    mForwardFolders.clear();
    mBackwardFolders.clear();
}

bool LLInventorySingleFolderPanel::isBackwardAvailable() const
{
    return !mBackwardFolders.empty() && (mFolderID != mBackwardFolders.back());
}

bool LLInventorySingleFolderPanel::isForwardAvailable() const
{
    return !mForwardFolders.empty() && (mFolderID != mForwardFolders.back());
}

boost::signals2::connection LLInventorySingleFolderPanel::setRootChangedCallback(root_changed_callback_t cb)
{
    return mRootChangedSignal.connect(cb);
}

void LLInventorySingleFolderPanel::updateSingleFolderRoot()
{
    if (mFolderID != getRootFolderID())
    {
        mRootChangedSignal();

        LLUUID root_id = mFolderID;
        if (mFolderRoot.get())
        {
            mItemMap.clear();
            mFolderRoot.get()->destroyRoot();
        }

        mCommitCallbackRegistrar.pushScope();
        {
            LLFolderView* folder_view = createFolderRoot(root_id);
            folder_view->setChildrenInited(false);
            mFolderRoot = folder_view->getHandle();
            mFolderRoot.get()->setSingleFolderMode(true);
            addItemID(root_id, mFolderRoot.get());

            LLRect scroller_view_rect = getRect();
            scroller_view_rect.translate(-scroller_view_rect.mLeft, -scroller_view_rect.mBottom);
            LLScrollContainer::Params scroller_params(mParams.scroll());
            scroller_params.rect(scroller_view_rect);

            if (mScroller)
            {
                removeChild(mScroller);
                delete mScroller;
                mScroller = NULL;
            }
            mScroller = LLUICtrlFactory::create<LLFolderViewScrollContainer>(scroller_params);
            addChild(mScroller);
            mScroller->addChild(mFolderRoot.get());
            mFolderRoot.get()->setScrollContainer(mScroller);
            mFolderRoot.get()->setFollowsAll();
            mFolderRoot.get()->addChild(mFolderRoot.get()->mStatusTextBox);

            if (mSelectionCallback != nullptr)
            {
                mFolderRoot.get()->setSelectCallback(mSelectionCallback);
            }
        }
        mCommitCallbackRegistrar.popScope();
        mFolderRoot.get()->setCallbackRegistrar(&mCommitCallbackRegistrar);

        buildNewViews(mFolderID);

        LLFloater* root_floater = gFloaterView->getParentFloater(this);
        if(root_floater)
        {
            root_floater->setFocus(true);
        }
    }
}

bool LLInventorySingleFolderPanel::hasVisibleItems() const
{
    if (const LLFolderView* root = mFolderRoot.get())
    {
        return root->hasVisibleChildren();
    }

    return false;
}

void LLInventorySingleFolderPanel::doCreate(const LLSD& userdata)
{
    std::string type_name = userdata.asString();
    LLUUID dest_id = LLFolderBridge::sSelf.get()->getUUID();
    if (("category" == type_name) || ("outfit" == type_name))
    {
        changeFolderRoot(dest_id);
    }
    reset_inventory_filter();
    menu_create_inventory_item(this, dest_id, userdata);
}

void LLInventorySingleFolderPanel::doToSelected(const LLSD& userdata)
{
    if (("open_in_current_window" == userdata.asString()))
    {
        changeFolderRoot(LLFolderBridge::sSelf.get()->getUUID());
        return;
    }
    LLInventoryPanel::doToSelected(userdata);
}

void LLInventorySingleFolderPanel::doShare()
{
    LLAvatarActions::shareWithAvatars(this);
}
/************************************************************************/
/* Asset Pre-Filtered Inventory Panel related class                     */
/************************************************************************/

LLAssetFilteredInventoryPanel::LLAssetFilteredInventoryPanel(const Params& p)
    : LLInventoryPanel(p)
{
}


void LLAssetFilteredInventoryPanel::initFromParams(const Params& p)
{
    // Init asset types
    std::string types = p.filter_asset_types.getValue();

    typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
    boost::char_separator<char> sep("|");
    tokenizer tokens(types, sep);
    tokenizer::iterator token_iter = tokens.begin();

    memset(mAssetTypes, 0, LLAssetType::AT_COUNT * sizeof(bool));
    while (token_iter != tokens.end())
    {
        const std::string& token_str = *token_iter;
        LLAssetType::EType asset_type = LLAssetType::lookup(token_str);
        if (asset_type > LLAssetType::AT_NONE && asset_type < LLAssetType::AT_COUNT)
        {
            mAssetTypes[asset_type] = true;
        }
        ++token_iter;
    }

    // Init drag types
    memset(mDragTypes, 0, EDragAndDropType::DAD_COUNT * sizeof(bool));
    for (S32 i = 0; i < LLAssetType::AT_COUNT; i++)
    {
        if (mAssetTypes[i])
        {
            EDragAndDropType drag_type = LLViewerAssetType::lookupDragAndDropType((LLAssetType::EType)i);
            if (drag_type != DAD_NONE)
            {
                mDragTypes[drag_type] = true;
            }
        }
    }
    // Always show AT_CATEGORY, but it shouldn't get into mDragTypes
    mAssetTypes[LLAssetType::AT_CATEGORY] = true;

    // Init the panel
    LLInventoryPanel::initFromParams(p);
    U64 filter_cats = getFilter().getFilterCategoryTypes();
    filter_cats &= ~(1ULL << LLFolderType::FT_MARKETPLACE_LISTINGS);
    getFilter().setFilterCategoryTypes(filter_cats);
    getFilter().setFilterNoMarketplaceFolder();
}

bool LLAssetFilteredInventoryPanel::handleDragAndDrop(S32 x, S32 y, MASK mask, bool drop,
    EDragAndDropType cargo_type,
    void* cargo_data,
    EAcceptance* accept,
    std::string& tooltip_msg)
{
    bool result = false;

    if (mAcceptsDragAndDrop)
    {
        // Don't allow DAD_CATEGORY here since it can contain other items besides required assets
        // We should see everything we drop!
        if (mDragTypes[cargo_type])
        {
            result = LLInventoryPanel::handleDragAndDrop(x, y, mask, drop, cargo_type, cargo_data, accept, tooltip_msg);
        }
    }

    return result;
}

/*virtual*/
bool LLAssetFilteredInventoryPanel::typedViewsFilter(const LLUUID& id, LLInventoryObject const* objectp)
{
    if (!objectp)
    {
        return false;
    }
    LLAssetType::EType asset_type = objectp->getType();

    if (asset_type < 0 || asset_type >= LLAssetType::AT_COUNT)
    {
        return false;
    }

    if (!mAssetTypes[asset_type])
    {
        return false;
    }

    return true;
}

void LLAssetFilteredInventoryPanel::itemChanged(const LLUUID& id, U32 mask, const LLInventoryObject* model_item)
{
    if (!model_item && !getItemByID(id))
    {
        // remove operation, but item is not in panel already
        return;
    }

    if (model_item)
    {
        LLAssetType::EType asset_type = model_item->getType();

        if (asset_type < 0
            || asset_type >= LLAssetType::AT_COUNT
            || !mAssetTypes[asset_type])
        {
            return;
        }
    }

    LLInventoryPanel::itemChanged(id, mask, model_item);
}

namespace LLInitParam
{
    void TypeValues<LLFolderType::EType>::declareValues()
    {
        declare(LLFolderType::lookup(LLFolderType::FT_TEXTURE)          , LLFolderType::FT_TEXTURE);
        declare(LLFolderType::lookup(LLFolderType::FT_SOUND)            , LLFolderType::FT_SOUND);
        declare(LLFolderType::lookup(LLFolderType::FT_CALLINGCARD)      , LLFolderType::FT_CALLINGCARD);
        declare(LLFolderType::lookup(LLFolderType::FT_LANDMARK)         , LLFolderType::FT_LANDMARK);
        declare(LLFolderType::lookup(LLFolderType::FT_CLOTHING)         , LLFolderType::FT_CLOTHING);
        declare(LLFolderType::lookup(LLFolderType::FT_OBJECT)           , LLFolderType::FT_OBJECT);
        declare(LLFolderType::lookup(LLFolderType::FT_NOTECARD)         , LLFolderType::FT_NOTECARD);
        declare(LLFolderType::lookup(LLFolderType::FT_ROOT_INVENTORY)   , LLFolderType::FT_ROOT_INVENTORY);
        declare(LLFolderType::lookup(LLFolderType::FT_LSL_TEXT)         , LLFolderType::FT_LSL_TEXT);
        declare(LLFolderType::lookup(LLFolderType::FT_BODYPART)         , LLFolderType::FT_BODYPART);
        declare(LLFolderType::lookup(LLFolderType::FT_TRASH)            , LLFolderType::FT_TRASH);
        declare(LLFolderType::lookup(LLFolderType::FT_SNAPSHOT_CATEGORY), LLFolderType::FT_SNAPSHOT_CATEGORY);
        declare(LLFolderType::lookup(LLFolderType::FT_LOST_AND_FOUND)   , LLFolderType::FT_LOST_AND_FOUND);
        declare(LLFolderType::lookup(LLFolderType::FT_ANIMATION)        , LLFolderType::FT_ANIMATION);
        declare(LLFolderType::lookup(LLFolderType::FT_GESTURE)          , LLFolderType::FT_GESTURE);
        declare(LLFolderType::lookup(LLFolderType::FT_FAVORITE)         , LLFolderType::FT_FAVORITE);
        declare(LLFolderType::lookup(LLFolderType::FT_ENSEMBLE_START)   , LLFolderType::FT_ENSEMBLE_START);
        declare(LLFolderType::lookup(LLFolderType::FT_ENSEMBLE_END)     , LLFolderType::FT_ENSEMBLE_END);
        declare(LLFolderType::lookup(LLFolderType::FT_CURRENT_OUTFIT)   , LLFolderType::FT_CURRENT_OUTFIT);
        declare(LLFolderType::lookup(LLFolderType::FT_OUTFIT)           , LLFolderType::FT_OUTFIT);
        declare(LLFolderType::lookup(LLFolderType::FT_MY_OUTFITS)       , LLFolderType::FT_MY_OUTFITS);
        declare(LLFolderType::lookup(LLFolderType::FT_MESH )            , LLFolderType::FT_MESH );
        declare(LLFolderType::lookup(LLFolderType::FT_INBOX)            , LLFolderType::FT_INBOX);
        declare(LLFolderType::lookup(LLFolderType::FT_OUTBOX)           , LLFolderType::FT_OUTBOX);
        declare(LLFolderType::lookup(LLFolderType::FT_BASIC_ROOT)       , LLFolderType::FT_BASIC_ROOT);
        declare(LLFolderType::lookup(LLFolderType::FT_SETTINGS)         , LLFolderType::FT_SETTINGS);
        declare(LLFolderType::lookup(LLFolderType::FT_MATERIAL)         , LLFolderType::FT_MATERIAL);
        declare(LLFolderType::lookup(LLFolderType::FT_MARKETPLACE_LISTINGS)   , LLFolderType::FT_MARKETPLACE_LISTINGS);
        declare(LLFolderType::lookup(LLFolderType::FT_MARKETPLACE_STOCK), LLFolderType::FT_MARKETPLACE_STOCK);
        declare(LLFolderType::lookup(LLFolderType::FT_MARKETPLACE_VERSION), LLFolderType::FT_MARKETPLACE_VERSION);
    }
}
