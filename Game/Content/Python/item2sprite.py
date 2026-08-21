import os
import unreal


# ============================================================
# CONFIGURATION
# ============================================================

# IMPORTANT:
# /Game already represents your project's Content folder.
#
# Physical:
#   D:/Python/Projects/Pokemon Solarus/Game/Content/Art/Bag
#
# Unreal:
#   /Game/Art/Bag
#
DESTINATION_ROOT = "/Game/Art/Bag"

# Search subfolders of the external image folder.
RECURSIVE = False

# beast -> beast_Sprite
SPRITE_NAME_SUFFIX = "_sprite"

# Don't recreate sprites that already exist.
SKIP_EXISTING_SPRITES = True

# Don't reimport textures that already exist.
SKIP_EXISTING_TEXTURES = True

SUPPORTED_IMAGE_EXTENSIONS = {
    ".png",
    ".jpg",
    ".jpeg",
    ".bmp",
    ".tga",
}


# ============================================================
# PATH HELPERS
# ============================================================

def normalize_unreal_path(path):
    """
    Normalize a Content Browser path.

    Examples:

        /All/Game/Art/Bag
            ->
        /Game/Art/Bag

    """

    path = str(path).strip().replace("\\", "/")
    path = path.rstrip("/")

    if path.startswith("/All/"):
        path = path[4:]

    return path


def to_unreal_destination_path(path_value):
    """
    Destination may be either:

        /Game/Art/Bag

    or a physical folder inside the project Content folder:

        D:/Project/Game/Content/Art/Bag

    Returns:

        /Game/Art/Bag
    """

    if not path_value:
        unreal.log_error(
            "[Sprite Generator] Destination root is required."
        )
        return None

    input_path = str(path_value).strip().replace("\\", "/")

    # --------------------------------------------------------
    # Already Unreal path
    # --------------------------------------------------------

    if input_path.startswith("/Game") or input_path.startswith("/All/Game"):
        result = normalize_unreal_path(input_path)

        # Prevent accidental /Game/Content/...
        if result.startswith("/Game/Content/"):
            unreal.log_warning(
                "[Sprite Generator] Destination contained "
                "'/Game/Content/'. Removing redundant 'Content'."
            )

            result = "/Game/" + result[len("/Game/Content/"):]

        return result

    # --------------------------------------------------------
    # Physical project Content path
    # --------------------------------------------------------

    abs_input = os.path.abspath(input_path).replace("\\", "/")

    content_dir = unreal.Paths.project_content_dir()
    content_dir = os.path.abspath(content_dir).replace("\\", "/")
    content_dir = content_dir.rstrip("/")

    if not abs_input.lower().startswith(content_dir.lower()):
        unreal.log_error(
            "[Sprite Generator] Destination filesystem path must "
            "be inside this project's Content folder.\n"
            f"Provided: {abs_input}\n"
            f"Content:  {content_dir}"
        )
        return None

    relative = abs_input[len(content_dir):].lstrip("/")

    if relative:
        return "/Game/" + relative

    return "/Game"


# ============================================================
# IMAGE DISCOVERY
# ============================================================

def find_image_files(source_disk_folder):
    """
    Find image files from a NORMAL Windows directory.

    Example:

        C:/Users/phong/Downloads/pokesprite/ball
    """

    source_disk_folder = os.path.abspath(source_disk_folder)

    if not os.path.isdir(source_disk_folder):
        unreal.log_error(
            "[Sprite Generator] Source image directory does not exist:\n"
            f"{source_disk_folder}"
        )
        return []

    images = []

    if RECURSIVE:

        for root, dirs, files in os.walk(source_disk_folder):

            for filename in files:

                extension = os.path.splitext(filename)[1].lower()

                if extension in SUPPORTED_IMAGE_EXTENSIONS:
                    images.append(
                        os.path.join(root, filename)
                    )

    else:

        for filename in os.listdir(source_disk_folder):

            full_path = os.path.join(
                source_disk_folder,
                filename
            )

            if not os.path.isfile(full_path):
                continue

            extension = os.path.splitext(filename)[1].lower()

            if extension in SUPPORTED_IMAGE_EXTENSIONS:
                images.append(full_path)

    return images


# ============================================================
# TEXTURE SETTINGS
# ============================================================

def apply_paper2d_texture_settings(texture):
    """
    Change ONLY:

    1. Texture Group
       -> 2D Pixels (unfiltered)

    2. Compression Settings
       -> Uncompressed (RGBA8)

    Nothing else is touched.
    """

    if not texture:
        return

    texture.modify()

    # --------------------------------------------------------
    # Texture Group = 2D Pixels (unfiltered)
    # --------------------------------------------------------

    texture.set_editor_property(
        "lod_group",
        unreal.TextureGroup.TEXTUREGROUP_PIXELS2D
    )

    # --------------------------------------------------------
    # Compression = Uncompressed (RGBA8)
    #
    # Unreal exposes the setting through Python as
    # TC_EDITOR_ICON.
    # --------------------------------------------------------

    texture.set_editor_property(
        "compression_settings",
        unreal.TextureCompressionSettings.TC_EDITOR_ICON
    )

    try:
        texture.post_edit_change()
    except Exception:
        pass

    unreal.EditorAssetLibrary.save_loaded_asset(
        texture,
        only_if_is_dirty=False
    )


# ============================================================
# IMPORT TEXTURES
# ============================================================

def import_images(image_files, destination_folder):
    """
    Import image files from disk as Texture2D assets.

    Example:

        C:/Downloads/pokesprite/ball/poke.png

    becomes:

        /Game/Art/Bag/ball/poke
    """

    if not unreal.EditorAssetLibrary.does_directory_exist(
        destination_folder
    ):
        unreal.EditorAssetLibrary.make_directory(
            destination_folder
        )

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

    tasks = []

    for image_path in image_files:

        filename_without_extension = os.path.splitext(
            os.path.basename(image_path)
        )[0]

        expected_asset_path = (
            destination_folder
            + "/"
            + filename_without_extension
        )

        # ----------------------------------------------------
        # Existing Texture2D
        # ----------------------------------------------------

        if (
            SKIP_EXISTING_TEXTURES
            and unreal.EditorAssetLibrary.does_asset_exist(
                expected_asset_path
            )
        ):
            unreal.log(
                "[Sprite Generator] Texture already exists: "
                f"{expected_asset_path}"
            )

            continue

        # ----------------------------------------------------
        # Import task
        # ----------------------------------------------------

        task = unreal.AssetImportTask()

        task.set_editor_property(
            "filename",
            os.path.abspath(image_path)
        )

        task.set_editor_property(
            "destination_path",
            destination_folder
        )

        task.set_editor_property(
            "automated",
            True
        )

        task.set_editor_property(
            "replace_existing",
            False
        )

        task.set_editor_property(
            "replace_existing_settings",
            False
        )

        task.set_editor_property(
            "save",
            True
        )

        tasks.append(task)

    # --------------------------------------------------------
    # Import everything
    # --------------------------------------------------------

    if tasks:

        unreal.log(
            f"[Sprite Generator] Importing {len(tasks)} images..."
        )

        asset_tools.import_asset_tasks(tasks)

    # --------------------------------------------------------
    # Now load every Texture2D in the destination folder.
    # This includes both newly imported AND existing textures.
    # --------------------------------------------------------

    asset_paths = unreal.EditorAssetLibrary.list_assets(
        destination_folder,
        recursive=False,
        include_folder=False
    )

    textures = []

    for asset_path in asset_paths:

        asset = unreal.EditorAssetLibrary.load_asset(
            asset_path
        )

        if isinstance(asset, unreal.Texture2D):
            textures.append(asset)

    return textures


# ============================================================
# CREATE SPRITE
# ============================================================

def create_sprite_from_texture(
    texture,
    destination_folder
):
    """
    Create one PaperSprite covering the whole Texture2D.
    """

    texture_name = texture.get_name()

    sprite_name = (
        texture_name
        + SPRITE_NAME_SUFFIX
    )

    sprite_asset_path = (
        destination_folder
        + "/"
        + sprite_name
    )

    # --------------------------------------------------------
    # Skip existing
    # --------------------------------------------------------

    if unreal.EditorAssetLibrary.does_asset_exist(
        sprite_asset_path
    ):

        if SKIP_EXISTING_SPRITES:

            unreal.log_warning(
                "[Sprite Generator] Sprite already exists, skipping: "
                f"{sprite_asset_path}"
            )

            return None

    # --------------------------------------------------------
    # Texture dimensions
    # --------------------------------------------------------

    width = texture.blueprint_get_size_x()
    height = texture.blueprint_get_size_y()

    # --------------------------------------------------------
    # Create PaperSprite
    # --------------------------------------------------------

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    sprite_factory = unreal.PaperSpriteFactory()

    sprite = asset_tools.create_asset(
        asset_name=sprite_name,
        package_path=destination_folder,
        asset_class=unreal.PaperSprite,
        factory=sprite_factory
    )

    if not sprite:

        unreal.log_error(
            "[Sprite Generator] Failed to create sprite: "
            f"{sprite_name}"
        )

        return None

    # --------------------------------------------------------
    # Configure sprite
    # --------------------------------------------------------

    sprite.modify()

    sprite.set_editor_property(
        "source_texture",
        texture
    )

    sprite.set_editor_property(
        "source_uv",
        unreal.Vector2D(
            x=0.0,
            y=0.0
        )
    )

    sprite.set_editor_property(
        "source_dimension",
        unreal.Vector2D(
            x=float(width),
            y=float(height)
        )
    )

    sprite.set_editor_property(
        "source_texture_dimension",
        unreal.Vector2D(
            x=float(width),
            y=float(height)
        )
    )

    sprite.set_editor_property(
        "source_image_dimension_before_trimming",
        unreal.Vector2D(
            x=float(width),
            y=float(height)
        )
    )

    sprite.set_editor_property(
        "origin_in_source_image_before_trimming",
        unreal.Vector2D(
            x=0.0,
            y=0.0
        )
    )

    # Bag icons do not need collision.
    sprite.set_editor_property(
        "sprite_collision_domain",
        unreal.SpriteCollisionMode.NONE
    )

    try:
        sprite.post_edit_change()
    except Exception:
        pass

    unreal.EditorAssetLibrary.save_loaded_asset(
        sprite,
        only_if_is_dirty=False
    )

    unreal.log(
        "[Sprite Generator] Created sprite: "
        f"{sprite_asset_path}"
    )

    return sprite


# ============================================================
# MAIN
# ============================================================

def generate_sprites_from_image_folder(
    source_disk_folder,
    destination_root=DESTINATION_ROOT
):
    """
    Example:

    generate_sprites_from_image_folder(
        r"C:\\Users\\phong\\Downloads\\pokesprite\\ball"
    )

    Creates:

        /Game/Art/Bag/ball
            -> Texture2D assets

        /Game/Art/Bag/ball_sprite
            -> PaperSprite assets
    """

    # --------------------------------------------------------
    # Check Paper2D
    # --------------------------------------------------------

    if not hasattr(unreal, "PaperSprite"):

        unreal.log_error(
            "[Sprite Generator] Paper2D is not enabled.\n"
            "Enable Paper 2D plugin and restart Unreal."
        )

        return

    # --------------------------------------------------------
    # Validate external source folder
    # --------------------------------------------------------

    source_disk_folder = os.path.abspath(
        source_disk_folder
    )

    if not os.path.isdir(source_disk_folder):

        unreal.log_error(
            "[Sprite Generator] Image directory does not exist:\n"
            f"{source_disk_folder}"
        )

        return

    # --------------------------------------------------------
    # Convert destination
    # --------------------------------------------------------

    destination_root = to_unreal_destination_path(
        destination_root
    )

    if not destination_root:
        return

    # --------------------------------------------------------
    # Folder names
    # --------------------------------------------------------

    folder_name = os.path.basename(
        os.path.normpath(source_disk_folder)
    )

    # Raw textures:
    #
    # /Game/Art/Bag/ball
    #
    texture_folder = (
        destination_root.rstrip("/")
        + "/"
        + folder_name
    )

    # Sprites:
    #
    # /Game/Art/Bag/ball_sprite
    #
    sprite_folder = (
        destination_root.rstrip("/")
        + "/"
        + folder_name
        + "_sprite"
    )

    unreal.log(
        "\n"
        "==========================================\n"
        "POKEMON ITEM SPRITE GENERATOR\n"
        "==========================================\n"
        f"Images:   {source_disk_folder}\n"
        f"Textures: {texture_folder}\n"
        f"Sprites:  {sprite_folder}\n"
        "=========================================="
    )

    # --------------------------------------------------------
    # Find images
    # --------------------------------------------------------

    image_files = find_image_files(
        source_disk_folder
    )

    if not image_files:

        unreal.log_warning(
            "[Sprite Generator] No supported image files found in:\n"
            f"{source_disk_folder}"
        )

        return

    unreal.log(
        f"[Sprite Generator] Found {len(image_files)} image files."
    )

    # --------------------------------------------------------
    # Import Texture2Ds
    # --------------------------------------------------------

    textures = import_images(
        image_files,
        texture_folder
    )

    if not textures:

        unreal.log_error(
            "[Sprite Generator] No Texture2D assets available after import."
        )

        return

    unreal.log(
        f"[Sprite Generator] Processing {len(textures)} textures."
    )

    # --------------------------------------------------------
    # Create sprite directory
    # --------------------------------------------------------

    if not unreal.EditorAssetLibrary.does_directory_exist(
        sprite_folder
    ):
        unreal.EditorAssetLibrary.make_directory(
            sprite_folder
        )

    # --------------------------------------------------------
    # Process
    # --------------------------------------------------------

    created_count = 0
    skipped_count = 0

    with unreal.ScopedSlowTask(
        len(textures),
        "Creating Pokemon item sprites..."
    ) as slow_task:

        slow_task.make_dialog(True)

        for texture in textures:

            if slow_task.should_cancel():
                unreal.log_warning(
                    "[Sprite Generator] Cancelled."
                )
                break

            slow_task.enter_progress_frame(
                1,
                f"Processing {texture.get_name()}"
            )

            # ------------------------------------------------
            # ONLY your two texture settings
            # ------------------------------------------------

            apply_paper2d_texture_settings(
                texture
            )

            # ------------------------------------------------
            # PaperSprite
            # ------------------------------------------------

            sprite = create_sprite_from_texture(
                texture,
                sprite_folder
            )

            if sprite:
                created_count += 1
            else:
                skipped_count += 1

    # --------------------------------------------------------
    # Save
    # --------------------------------------------------------

    unreal.EditorAssetLibrary.save_directory(
        texture_folder,
        only_if_is_dirty=False,
        recursive=True
    )

    unreal.EditorAssetLibrary.save_directory(
        sprite_folder,
        only_if_is_dirty=False,
        recursive=True
    )

    unreal.EditorUtilityLibrary.sync_browser_to_folders(
        [sprite_folder]
    )