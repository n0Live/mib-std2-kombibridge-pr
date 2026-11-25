/*
 * Decompiled with CFR 0.152 from the stock class (MIBHMI-fixed.jar), then changed:
 * navsd-shadow delta (ZOOM_TO_AA): while Android Auto's map is on the AID, the
 * cluster's zoom requests go to the phone as zoom keys (de.aatokombi.ClusterVideo.sendCommand)
 * instead of to the unit's own map, and the status reports auto zoom off (the cluster only
 * sends "auto zoom on", the OK of its zoom question, when it is off). Every request is logged.
 * Otherwise stock.
 */
package de.vw.mib.bap.mqbab2.navsd.functions;

import de.vw.mib.bap.datatypes.BAPEntity;
import de.vw.mib.bap.functions.BAPFunctionListener;
import de.vw.mib.bap.functions.Property;
import de.vw.mib.bap.functions.PropertyListener;
import de.vw.mib.bap.mqbab2.common.api.navigation.NavigationService;
import de.vw.mib.bap.mqbab2.common.api.navigation.NavigationServiceListener;
import de.vw.mib.bap.mqbab2.common.api.navigation.datatypes.NavigationMapScale;
import de.vw.mib.bap.mqbab2.common.api.stages.BAPStage;
import de.vw.mib.bap.mqbab2.common.api.stages.BAPStageInitializer;
import de.vw.mib.bap.mqbab2.common.api.stages.Function;
import de.vw.mib.bap.mqbab2.generated.navsd.serializer.MapScale_SetGet;
import de.vw.mib.bap.mqbab2.generated.navsd.serializer.MapScale_Status;
import de.vw.mib.bap.mqbab2.navsd.api.ASLNavSDConstants;
import de.aatokombi.ClusterVideo;
import de.aatokombi.Config;

public final class MapScale
extends Function
implements Property,
ASLNavSDConstants,
NavigationServiceListener {
    private static final int VALUE_AUTOZOOM_DISABLED = 0;
    private static final int VALUE_AUTOZOOM_ENABLED = 1;
    private static final int VALUE_CONTINUOUS_ZOOM_STOP = 0;
    private static final int VALUE_CONTINUOUS_ZOOM_IN = 127;
    private static final int VALUE_CONTINUOUS_ZOOM_OUT = -128;
    private static final int VALUE_ZOOM_IN = 1;
    private static final int VALUE_ZOOM_OUT = -1;
    protected static final int[] NAVIGATION_LISTENER_IDS = new int[]{3798};
    private static final int ZOOM_TYPE_STEP = 1;
    private static final int ZOOM_TYPE_CONTINUOUS = 2;
    private static final int ZOOM_TYPE_CONTINUOUS_STOP = 5;

    /**
     * EXPERIMENTAL, and not working on the car so far (2026-11): while the phone's map is on the AID,
     * the wheel zoom (BAP MapScale: steps +-1, or continuous 127 / -128 until 0) goes to Android
     * Auto as keys on the cluster input channel instead of to the unit's own map. Every MapScale
     * request is logged. Car 2026-11-03: the phone ignored KEYCODE_ZOOM_IN/OUT on the cluster input
     * (196 sent, no zoom); 2026-11-06 the rotary controller did not zoom either (32 sent); mib-
     * cluster-map and mib2q-carplay report that Android Auto's cluster map is not interactive.
     * Set false to leave the zoom to the unit's own map.
     */
    private static final boolean ZOOM_TO_AA = true;
    /** Android KEYCODE_ZOOM_IN / KEYCODE_ZOOM_OUT, advertised by the libgal on the cluster input. */
    private static final int KEY_ZOOM_IN = 168;
    private static final int KEY_ZOOM_OUT = 169;
    /** true: BAP "+1" means zoom OUT (swap if the car zooms the wrong way). */
    private static final boolean ZOOM_INVERT = false;
    /** true: zoom as the AA rotary controller (keycode 65536, +-1 per step) instead of the zoom keys. */
    private static final boolean ZOOM_ROTARY = false;
    /** Repeat interval of a zoom key while the wheel is held (continuous zoom), ms. */
    private static final int ZOOM_REPEAT_MS = 400;
    private static final int ZOOM_TYPE_AUTOZOOM = 3;
    private int currentZoomType = 1;
    private static volatile MapScale INSTANCE = null;
    private static boolean lastAA;

    /**
     * The AA map came on or went off the AID: send the status again, so the cluster sees the
     * auto zoom state that belongs to the map now shown (off on the AA map, the unit's own
     * value on its map). Any thread; never throws.
     */
    public static void aaMapChanged(boolean aa) {
        if (aa == lastAA) return;
        lastAA = aa;
        MapScale i = INSTANCE;
        if (i == null || !ZOOM_TO_AA) return;
        try {
            ClusterVideo.log("MapScale status again: " + (aa ? "AA map, auto zoom reported off" : "unit's map, its own auto zoom"));
            i.process(-1);
        } catch (Throwable t) {
            de.aatokombi.Quiet.note("MapScale#1", t);
        }
    }
    private PropertyListener listener;
    static /* synthetic */ Class class$de$vw$mib$bap$mqbab2$generated$navsd$serializer$MapScale_Status;

    public BAPEntity init(BAPStageInitializer bAPStageInitializer) {
        this.getNavigationService().addNavigationServiceListener(this, NAVIGATION_LISTENER_IDS);
        INSTANCE = this;
        return this.computeMapScaleStatus();
    }

    protected MapScale_Status dequeueBAPEntity() {
        return (MapScale_Status)this.context.dequeueBAPEntity(this, class$de$vw$mib$bap$mqbab2$generated$navsd$serializer$MapScale_Status == null ? (class$de$vw$mib$bap$mqbab2$generated$navsd$serializer$MapScale_Status = MapScale.class$("de.vw.mib.bap.mqbab2.generated.navsd.serializer.MapScale_Status")) : class$de$vw$mib$bap$mqbab2$generated$navsd$serializer$MapScale_Status);
    }

    public void setFunctionData(BAPStage bAPStage, Object object) {
    }

    public int getFunctionId() {
        return 45;
    }

    public void initialize(boolean bl) {
    }

    public void uninitialize() {
        this.getNavigationService().removeNavigationServiceListener(this, NAVIGATION_LISTENER_IDS);
    }

    public void process(int n) {
        boolean bl = this.getDelegate().getPropertyListener(this).statusProperty(this.computeMapScaleStatus(), this);
        if (this.listener != null && !bl) {
            this.listener.statusProperty(this.computeMapScaleStatus(), this);
        }
        this.listener = null;
    }

    public void getProperty(BAPEntity bAPEntity, PropertyListener propertyListener) {
        propertyListener.requestError(65, this);
    }

    public void setGetProperty(BAPEntity bAPEntity, PropertyListener propertyListener) {
        this.listener = propertyListener;
        if (bAPEntity instanceof MapScale_SetGet) {
            MapScale_SetGet mapScale_SetGet = (MapScale_SetGet)bAPEntity;
            NavigationService navigationService = this.getNavigationService();
            if (this.isBapSetGetParametersValid(mapScale_SetGet)) {
                int n = mapScale_SetGet.steps;
                int n2 = mapScale_SetGet.autoZoom;
                int n3 = this.getTypeFromValues(n, n2);
                boolean toAA = ZOOM_TO_AA && ClusterVideo.alive();
                ClusterVideo.log("MapScale SetGet steps=" + n + " autoZoom=" + n2 + " type=" + n3
                        + " aaVideo=" + ClusterVideo.alive() + (toAA ? " -> AA" : " -> stock"));
                if (n3 == 3) {
                    // auto zoom on = the OK of the scale mode (the right roller press), the only trace of that key
                    // that reaches the unit; with ModeKey.OK it steps the cluster mode instead of zooming
                    boolean stepped = de.aatokombi.WheelKeys.okSeen("MapScale autoZoom", toAA);
                    if (stepped) {
                        propertyListener.statusProperty(this.computeMapScaleStatus(), this);
                        return;
                    }
                }
                if (toAA) {
                    MapScale.zoomAA(n3, n);
                    this.currentZoomType = n3;
                    propertyListener.statusProperty(this.computeMapScaleStatus(), this);
                    return;
                }
                switch (n3) {
                    case 3:
                    case 5: {
                        navigationService.setMapScaleSteps(0, n2);
                        break;
                    }
                    case 2: {
                        navigationService.setMapScaleSteps(n, n2);
                        break;
                    }
                    case 1: {
                        if (this.currentZoomType == 2) break;
                        navigationService.setMapScaleSteps(n, n2);
                        break;
                    }
                }
                this.currentZoomType = n3;
                propertyListener.statusProperty(this.computeMapScaleStatus(), this);
            } else {
                propertyListener.requestError(65, this);
            }
        } else {
            propertyListener.requestError(69, this);
        }
    }

    /** Zoom request -> phone keys: step = one key, continuous = repeated key until stop. */
    private static void zoomAA(int type, int steps) {
        boolean in = steps > 0;
        if (ZOOM_INVERT) in = !in;
        int key = in ? KEY_ZOOM_IN : KEY_ZOOM_OUT;
        if (ZOOM_ROTARY) {
            // the AA rotary controller (keycode 65536, relative), the only zoom-like input the
            // DHU's cluster declares; one detent per BAP step, continuous zoom as one detent
            if (type == 1 || type == 2) {
                ClusterVideo.sendCommand("rot 65536 " + (in ? 1 : -1));
            }
            return;
        }
        switch (type) {
            case 1: {
                ClusterVideo.sendCommand("key " + key);
                break;
            }
            case 2: {
                ClusterVideo.sendCommand("rep " + key + " " + ZOOM_REPEAT_MS);
                break;
            }
            default: {
                ClusterVideo.sendCommand("rep 0 0");   // stop, or autozoom: nothing to forward
            }
        }
    }

    private int getTypeFromValues(int n, int n2) {
        int n3 = 1;
        n3 = n2 == 1 ? 3 : (n == -128 || n == 127 ? 2 : (n == 0 ? 5 : 1));
        return n3;
    }

    public void ackProperty(BAPEntity bAPEntity, PropertyListener propertyListener) {
        propertyListener.requestError(65, this);
    }

    public void indicationError(int n, BAPFunctionListener bAPFunctionListener) {
    }

    public void requestAcknowledge() {
    }

    public void errorAcknowledge() {
    }

    private boolean isBapSetGetParametersValid(MapScale_SetGet mapScale_SetGet) {
        return !(mapScale_SetGet.steps != -128 && mapScale_SetGet.steps != 127 && mapScale_SetGet.steps != -1 && mapScale_SetGet.steps != 1 && mapScale_SetGet.steps != 0 || mapScale_SetGet.autoZoom != 0 && mapScale_SetGet.autoZoom != 1);
    }

    private MapScale_Status computeMapScaleStatus() {
        MapScale_Status mapScale_Status = this.dequeueBAPEntity();
        NavigationMapScale navigationMapScale = this.getNavigationService().getMapScale();
        mapScale_Status.scale = navigationMapScale.getMapScaleScale();
        mapScale_Status.unit = navigationMapScale.getMapScaleScaleUnit();
        // on the AA map the unit's own auto zoom does not apply: report it off, so the OK of the zoom
        // question asks for it (and can step the cluster mode); the unit's value itself is never changed
        mapScale_Status.autoZoomState.autoZoomActive = !(ZOOM_TO_AA && ClusterVideo.alive())
                && navigationMapScale.getMapScaleAutozoom() == 1;
        return mapScale_Status;
    }

    public void processHMIEvent(int n) {
    }

    public void updateNavigationData(NavigationService navigationService, int n) {
        this.process(-1);
    }

    static /* synthetic */ Class class$(String string) {
        try {
            return Class.forName(string);
        }
        catch (ClassNotFoundException classNotFoundException) {
            throw new NoClassDefFoundError(classNotFoundException.getMessage());
        }
    }
}
