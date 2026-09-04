.class public Lcom/dynamixsoftware/printershare/M100DExtractor;
.super Ljava/lang/Object;


# Copy a single file from assets to internal storage
.method private static copyAsset(Landroid/content/res/AssetManager;Ljava/lang/String;Ljava/io/File;)V
    .locals 7
    const/4 v6, 0x0
    const/4 v5, 0x0
    const/4 v4, 0x0
    const/4 v3, 0x0
    const/4 v2, 0x0
    const/4 v1, 0x0
    const/4 v0, 0x0

    :try_start_0
    invoke-virtual {p0, p1}, Landroid/content/res/AssetManager;->open(Ljava/lang/String;)Ljava/io/InputStream;
    move-result-object v0
    :try_end_0
    .catch Ljava/lang/Exception; {:try_start_0 .. :try_end_0} :catch_0

    :try_start_1
    new-instance v1, Ljava/io/FileOutputStream;
    invoke-direct {v1, p2}, Ljava/io/FileOutputStream;-><init>(Ljava/io/File;)V
    :try_end_1
    .catch Ljava/lang/Exception; {:try_start_1 .. :try_end_1} :catch_1

    const/16 v2, 0x1000
    new-array v2, v2, [B

    :goto_0
    :try_start_2
    invoke-virtual {v0, v2}, Ljava/io/InputStream;->read([B)I
    move-result v3
    :try_end_2
    .catch Ljava/lang/Exception; {:try_start_2 .. :try_end_2} :catch_2

    if-gtz v3, :cond_0

    :try_start_3
    const/4 v4, 0x0
    invoke-virtual {v1, v2, v4, v3}, Ljava/io/OutputStream;->write([BII)V
    :try_end_3
    .catch Ljava/lang/Exception; {:try_start_3 .. :try_end_3} :catch_3

    goto :goto_0

    :cond_0
    :try_start_4
    invoke-virtual {v0}, Ljava/io/InputStream;->close()V
    :try_end_4
    .catch Ljava/lang/Exception; {:try_start_4 .. :try_end_4} :catch_4

    :try_start_5
    invoke-virtual {v1}, Ljava/io/OutputStream;->close()V
    :try_end_5
    .catch Ljava/lang/Exception; {:try_start_5 .. :try_end_5} :catch_5

    return-void

    :catch_0
    :catch_1
    :catch_2
    :catch_3
    :catch_4
    :catch_5
    return-void
.end method


# Main extraction method
.method public static extract(Landroid/content/Context;)V
    .locals 11
    const/4 v10, 0x0
    const/4 v9, 0x0
    const/4 v8, 0x0
    const/4 v7, 0x0
    const/4 v6, 0x0
    const/4 v5, 0x0
    const/4 v4, 0x0
    const/4 v3, 0x0
    const/4 v2, 0x0
    const/4 v1, 0x0
    const/4 v0, 0x0

    :try_start_0
    # v0 = context.getFilesDir()
    invoke-virtual {p0}, Landroid/content/Context;->getFilesDir()Ljava/io/File;
    move-result-object v0

    # v1 = new File(filesDir, "drv_m100d")
    new-instance v1, Ljava/io/File;
    const-string v2, "drv_m100d"
    invoke-direct {v1, v0, v2}, Ljava/io/File;-><init>(Ljava/io/File;Ljava/lang/String;)V
    invoke-virtual {v1}, Ljava/io/File;->mkdirs()Z

    # Detect arch - check first ABI
    sget-object v0, Landroid/os/Build;->SUPPORTED_ABIS:[Ljava/lang/String;
    const/4 v2, 0x0
    aget-object v0, v0, v2

    const-string v2, "arm64"
    invoke-virtual {v0, v2}, Ljava/lang/String;->contains(Ljava/lang/CharSequence;)Z
    move-result v0

    if-eqz v0, :cond_arm32

    # ARM64 paths
    const-string v2, "drv_m100d_lib/libm100d_arm64.so"
    const-string v3, "drv_m100d_lib/rastertolhplh_arm64"
    :goto_arch_done

    # v2 = source path of libm100d.so in assets
    # v3 = source path of rastertolhplh in assets
    # v1 = target directory

    # Get AssetManager
    invoke-virtual {p0}, Landroid/content/Context;->getAssets()Landroid/content/res/AssetManager;
    move-result-object v4

    # Target file: <target_dir>/libm100d.so
    new-instance v5, Ljava/io/File;
    const-string v6, "libm100d.so"
    invoke-direct {v5, v1, v6}, Ljava/io/File;-><init>(Ljava/io/File;Ljava/lang/String;)V

    # Copy libm100d.so
    invoke-static {v4, v2, v5}, Lcom/dynamixsoftware/printershare/M100DExtractor;->copyAsset(Landroid/content/res/AssetManager;Ljava/lang/String;Ljava/io/File;)V

    # Set executable permission
    const/4 v6, 0x1
    const/4 v7, 0x0
    invoke-virtual {v5, v6, v7}, Ljava/io/File;->setExecutable(ZZ)Z
    invoke-virtual {v5, v6, v7}, Ljava/io/File;->setReadable(ZZ)Z

    # Target file: <target_dir>/rastertolhplh
    new-instance v5, Ljava/io/File;
    const-string v6, "rastertolhplh"
    invoke-direct {v5, v1, v6}, Ljava/io/File;-><init>(Ljava/io/File;Ljava/lang/String;)V

    # Copy rastertolhplh
    invoke-static {v4, v3, v5}, Lcom/dynamixsoftware/printershare/M100DExtractor;->copyAsset(Landroid/content/res/AssetManager;Ljava/lang/String;Ljava/io/File;)V

    # Set executable permission
    invoke-virtual {v5, v6, v7}, Ljava/io/File;->setExecutable(ZZ)Z
    invoke-virtual {v5, v6, v7}, Ljava/io/File;->setReadable(ZZ)Z

    # Create ppd subdirectory
    new-instance v8, Ljava/io/File;
    const-string v6, "ppd"
    invoke-direct {v8, v1, v6}, Ljava/io/File;-><init>(Ljava/io/File;Ljava/lang/String;)V
    invoke-virtual {v8}, Ljava/io/File;->mkdirs()Z

    # Loop through PPD files (hardcoded)
    const-string v2, "lenovo-M100D-arm.ppd"
    new-instance v5, Ljava/io/File;
    invoke-direct {v5, v8, v2}, Ljava/io/File;-><init>(Ljava/io/File;Ljava/lang/String;)V
    invoke-static {v4, v2, v5}, Lcom/dynamixsoftware/printershare/M100DExtractor;->copyAsset(Landroid/content/res/AssetManager;Ljava/lang/String;Ljava/io/File;)V

    const-string v2, "lenovo-L100D-arm.ppd"
    new-instance v5, Ljava/io/File;
    invoke-direct {v5, v8, v2}, Ljava/io/File;-><init>(Ljava/io/File;Ljava/lang/String;)V
    invoke-static {v4, v2, v5}, Lcom/dynamixsoftware/printershare/M100DExtractor;->copyAsset(Landroid/content/res/AssetManager;Ljava/lang/String;Ljava/io/File;)V

    const-string v2, "lenovo-L100DW-arm.ppd"
    new-instance v5, Ljava/io/File;
    invoke-direct {v5, v8, v2}, Ljava/io/File;-><init>(Ljava/io/File;Ljava/lang/String;)V
    invoke-static {v4, v2, v5}, Lcom/dynamixsoftware/printershare/M100DExtractor;->copyAsset(Landroid/content/res/AssetManager;Ljava/lang/String;Ljava/io/File;)V

    const-string v2, "lenovo-M100DNA-arm.ppd"
    new-instance v5, Ljava/io/File;
    invoke-direct {v5, v8, v2}, Ljava/io/File;-><init>(Ljava/io/File;Ljava/lang/String;)V
    invoke-static {v4, v2, v5}, Lcom/dynamixsoftware/printershare/M100DExtractor;->copyAsset(Landroid/content/res/AssetManager;Ljava/lang/String;Ljava/io/File;)V

    const-string v2, "lenovo-M1520D-arm.ppd"
    new-instance v5, Ljava/io/File;
    invoke-direct {v5, v8, v2}, Ljava/io/File;-><init>(Ljava/io/File;Ljava/lang/String;)V
    invoke-static {v4, v2, v5}, Lcom/dynamixsoftware/printershare/M100DExtractor;->copyAsset(Landroid/content/res/AssetManager;Ljava/lang/String;Ljava/io/File;)V

    const-string v2, "lenovo-M1688DW-arm.ppd"
    new-instance v5, Ljava/io/File;
    invoke-direct {v5, v8, v2}, Ljava/io/File;-><init>(Ljava/io/File;Ljava/lang/String;)V
    invoke-static {v4, v2, v5}, Lcom/dynamixsoftware/printershare/M100DExtractor;->copyAsset(Landroid/content/res/AssetManager;Ljava/lang/String;Ljava/io/File;)V

    :try_end_0
    .catch Ljava/lang/Exception; {:try_start_0 .. :try_end_0} :catch_all

    return-void

    :cond_arm32
    # ARM32 paths
    const-string v2, "drv_m100d_lib/libm100d_arm32.so"
    const-string v3, "drv_m100d_lib/rastertolhplh_arm32"
    goto :goto_arch_done

    :catch_all
    move-exception v0
    invoke-virtual {v0}, Ljava/lang/Throwable;->printStackTrace()V
    return-void
.end method
