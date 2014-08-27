﻿// mgsnapimpl.cpp: 实现命令管理器类
// Copyright (c) 2004-2013, Zhang Yungui
// License: LGPL, https://github.com/rhcad/touchvg

#include "mgcmdmgr_.h"
#include "mgbasicsps.h"

class SnapItem {
public:
    Point2d pt;             // 捕捉到的坐标
    Point2d base;           // 参考线基准点、原始点
    float   maxdist;        // 最大容差
    float   dist;           // 捕捉距离
    int     type;           // 特征点类型
    int     shapeid;        // 捕捉到的图形
    int     handleIndex;    // 捕捉到图形上的控制点序号，最近点和垂足则为边序号
    int     handleIndexSrc; // 待确定位置的源图形上的控制点序号，与handleIndex点匹配
    
    SnapItem() {}
    SnapItem(const Point2d& _pt, const Point2d& _base, float _dist, int _type = 0,
        int _shapeid = 0, int _handleIndex = -1, int _handleIndexSrc = -1)
        : pt(_pt), base(_base), maxdist(_dist), dist(_dist), type(_type), shapeid(_shapeid)
        , handleIndex(_handleIndex), handleIndexSrc(_handleIndexSrc) {}
};

static int snapHV(const Point2d& basePt, Point2d& newPt, SnapItem arr[3])
{
    int ret = 0;
    float diff;
    
    diff = arr[1].dist - fabsf(newPt.x - basePt.x);
    if (diff > _MGZERO || (diff > - _MGZERO
                           && fabsf(newPt.y - basePt.y) < fabsf(newPt.y - arr[1].base.y))) {
        arr[1].dist = fabsf(newPt.x - basePt.x);
        arr[1].base = basePt;
        newPt.x = basePt.x;
        arr[1].pt = newPt;
        arr[1].type = kMgSnapSameX;
        ret |= 1;
    }
    diff = arr[2].dist - fabsf(newPt.y - basePt.y);
    if (diff > _MGZERO || (diff > - _MGZERO
                     && fabsf(newPt.x - basePt.x) < fabsf(newPt.x - arr[2].base.x))) {
        arr[2].dist = fabsf(newPt.y - basePt.y);
        arr[2].base = basePt;
        newPt.y = basePt.y;
        arr[2].pt = newPt;
        arr[2].type = kMgSnapSameY;
        ret |= 2;
    }
    
    return ret;
}

static bool skipShape(const int* ignoreids, const MgShape* sp)
{
    bool skip = sp->shapec()->getFlag(kMgNoSnap);
    for (int t = 0; ignoreids[t] != 0 && !skip; t++) {
        skip = (ignoreids[t] == sp->getID());           // 跳过当前图形
    }
    return skip;
}

static bool snapHandle(const MgMotion*, const Point2d& orignPt,
                       const MgShape* shape, int ignoreHandle,
                       const MgShape* sp, SnapItem& arr0, Point2d* matchpt)
{
    int n = sp->shapec()->getHandleCount();
    bool curve = sp->shapec()->isKindOf(MgSplines::Type());
    bool dragHandle = (!shape || shape->getID() == 0 ||
                       orignPt == shape->shapec()->getHandlePoint(ignoreHandle));
    bool handleFound = false;
    
    for (int i = 0; i < n; i++) {                    // 循环每一个控制点
        if (curve && ((i > 0 && i + 1 < n) || sp->shapec()->isClosed())) {
            continue;                                   // 对于开放曲线只捕捉端点
        }
        Point2d pnt(sp->shapec()->getHandlePoint(i));   // 已有图形的一个顶点
        int handleType = sp->shapec()->getHandleType(i);
        
        float dist = pnt.distanceTo(orignPt);           // 触点与顶点匹配
        if (arr0.dist > dist && handleType < kMgHandleOutside && n > 1
            && !(shape && shape->getID() == 0 && pnt == shape->shapec()->getPoint(0))) {
            arr0.dist = dist;
            arr0.base = orignPt;
            arr0.pt = pnt;
            arr0.type = kMgSnapPoint + handleType - kMgHandleVertext;
            arr0.shapeid = sp->getID();
            arr0.handleIndex = i;
            arr0.handleIndexSrc = dragHandle ? ignoreHandle : -1;
            handleFound = true;
        }
        
        int d = matchpt ? shape->shapec()->getHandleCount() - 1 : -1;
        for (; d >= 0; d--) {                           // 整体移动图形，顶点匹配
            if (d == ignoreHandle || shape->shapec()->isHandleFixed(d))
                continue;
            Point2d ptd (shape->shapec()->getHandlePoint(d));   // 当前图形的顶点
            
            dist = pnt.distanceTo(ptd);                 // 当前图形与其他图形顶点匹配
            if ((arr0.type == kMgSnapNearPt || arr0.dist > dist - _MGZERO)
                && handleType < kMgHandleOutside) {
                arr0.dist = dist;
                arr0.base = ptd;  // 新的移动起点为当前图形的一个顶点
                arr0.pt = pnt;    // 将从ptd移到其他图形顶点pnt
                arr0.type = kMgSnapPoint + handleType - kMgHandleVertext;
                arr0.shapeid = sp->getID();
                arr0.handleIndex = i;
                arr0.handleIndexSrc = d;
                handleFound = true;
                
                // 因为对当前图形先从startM移到pointM，然后再从pointM移到matchpt
                *matchpt = orignPt + (pnt - ptd);       // 所以最后差量为(pnt-ptd)
            }
        }
    }
    
    return handleFound;
}

static bool snapPerp(const MgMotion* sender, const Point2d& orignPt, const Tol& tol,
                     const MgShape* shape, const MgShape* sp, SnapItem& arr0)
{
    int ret = -1;
    
    if (shape && shape->getID() == 0 && shape->shapec()->isKindOf(MgLine::Type()) // 正画的线段
        && !sp->shapec()->isCurve()) {
        const MgBaseShape* s = sp->shapec();
        int n = s->getPointCount();
        int edges = n - (s->isClosed() ? 0 : 1);            // 边数
        Point2d perp1, perp2;
        Point2d start(shape->shapec()->getPoint(0));
        
        for (int i = 0; i < edges; i++) {
            Point2d pt1(s->getHandlePoint(i));
            Point2d pt2(s->getHandlePoint((i + 1) % n));
            float d2 = mglnrel::ptToBeeline2(pt1, pt2, orignPt, perp2);
            
            if (mglnrel::isColinear2(pt1, pt2, start, tol)) {   // 起点在线上
                float dist = perp2.distanceTo(start) * 2;
                if (d2 > 2 * arr0.maxdist && arr0.dist > dist) {
                    arr0.dist = dist;
                    arr0.base = start;
                    arr0.pt = start + orignPt - perp2.asVector();
                    arr0.type = kMgSnapPerp;
                    arr0.shapeid = sp->getID();
                    arr0.handleIndex = i;
                    arr0.handleIndexSrc = 1;
                    ret = i;
                }
            } else if (d2 < arr0.maxdist) {                 // 终点在线附近
                mglnrel::ptToBeeline2(pt1, pt2, start, perp1);
                float dist = perp1.distanceTo(orignPt);
                if (arr0.type == kMgSnapNearPt || arr0.dist > dist) {
                    arr0.dist = dist;
                    arr0.base = perp1;
                    arr0.pt = perp1;
                    arr0.type = kMgSnapPerp;
                    arr0.shapeid = sp->getID();
                    arr0.handleIndex = i;
                    arr0.handleIndexSrc = 1;
                    ret = i;
                }
            }
        }
    }
    
    return ret >= 0;
}

static void snapNear(const MgMotion* sender, const Point2d& orignPt,
                     const MgShape* shape, int ignoreHandle, float tolNear,
                     const MgShape* sp, SnapItem& arr0, Point2d* matchpt)
{
    if (arr0.type >= kMgSnapPoint && arr0.type < kMgSnapNearPt) {
        return;
    }
    
    Point2d nearpt, ptd;
    MgHitResult res;
    float dist;
    float minDist = arr0.dist;
    int d = matchpt ? shape->shapec()->getHandleCount() : 0;
    
    for (; d >= 0; d--) {       // 对需定位的图形(shape)的每个控制点和当前触点
        if (d == 0) {
            ptd = orignPt;      // 触点与边匹配
        }
        else {
            if (d - 1 == ignoreHandle || shape->shapec()->isHandleFixed(d - 1))
                continue;
            ptd = shape->shapec()->getHandlePoint(d - 1);   // 控制点与边匹配
        }
        dist = sp->shapec()->hitTest(ptd, tolNear, res);
        
        if (minDist > dist) {
            minDist = dist;
            arr0.base = ptd;            // 新的移动起点为当前图形的一个顶点
            arr0.pt = res.nearpt;       // 将从ptd移到其他图形顶点pnt
            arr0.type = kMgSnapNearPt;
            arr0.shapeid = sp->getID(); // 最近点在此图形上
            arr0.handleIndex = res.segment;
            arr0.handleIndexSrc = d - 1;
            if (d > 0) {    // 因为对当前图形先从startM移到pointM，然后再从pointM移到matchpt
                *matchpt = orignPt + (nearpt - ptd);
            }
        }
    }
    if (arr0.dist > minDist + _MGZERO) {
        arr0.dist = minDist + sender->displayMmToModel(4.f);
    }
}

static void snapGrid(const MgMotion*, const Point2d& orignPt,
                     const MgShape* shape, int ignoreHandle,
                     const MgShape* sp, SnapItem arr[3], Point2d* matchpt)
{
    if (sp->shapec()->isKindOf(MgGrid::Type())) {
        Point2d newPt (orignPt);
        const MgGrid* grid = (const MgGrid*)(sp->shapec());
        
        Point2d dists(arr[1].dist, arr[2].dist);
        int type = grid->snap(newPt, dists);
        if (type & 1) {
            arr[1].base = newPt;
            arr[1].pt = newPt;
            arr[1].type = kMgSnapGridX;
            arr[1].dist = dists.x;
        }
        if (type & 2) {
            arr[2].base = newPt;
            arr[2].pt = newPt;
            arr[2].type = kMgSnapGridY;
            arr[2].dist = dists.y;
        }
        
        int d = matchpt ? shape->shapec()->getHandleCount() - 1 : -1;
        for (; d >= 0; d--) {
            if (d == ignoreHandle || shape->shapec()->isHandleFixed(d))
                continue;
            
            Point2d ptd (shape->shapec()->getHandlePoint(d));
            dists.set(mgMin(arr[0].dist, arr[1].dist), mgMin(arr[0].dist, arr[2].dist));
            
            newPt = ptd;
            type = grid->snap(newPt, dists);
            float dist = newPt.distanceTo(ptd);
            
            if ((type & 3) == 3 && arr[0].dist > dist - _MGZERO) {
                arr[0].dist = dist;
                arr[0].base = ptd;
                arr[0].pt = newPt;
                arr[0].type = kMgSnapGrid;
                arr[0].shapeid = sp->getID();
                arr[0].handleIndex = -1;
                arr[0].handleIndexSrc = d;
                
                // 因为对当前图形先从startM移到pointM，然后再从pointM移到matchpt
                *matchpt = orignPt + (newPt - ptd);     // 所以最后差量为(pnt-ptd)
            }
        }
    }
}

static void snapPoints(const MgMotion* sender, const Point2d& orignPt,
                       const MgShape* shape, int ignoreHandle,
                       const int* ignoreids, SnapItem arr[3], Point2d* matchpt)
{
    Box2d snapbox(orignPt, 2 * arr[0].dist, 0);         // 捕捉容差框
    GiTransform* xf = sender->view->xform();
    Box2d wndbox(xf->getWndRectM());
    MgShapeIterator it(sender->view->shapes());
    bool needSnapHandle = !!sender->view->getOptionInt("snap", "snapHandle", 1);
    bool needSnapNear = !!sender->view->getOptionInt("snap", "snapNear", 1);
    bool needSnapPerp = !!sender->view->getOptionInt("snap", "snapPerp", 0);
    float tolNear = sender->displayMmToModel("snap", "snapNearTol", 1);
    Tol tolPerp(sender->displayMmToModel(1));
    
    while (const MgShape* sp = it.getNext()) {
        if (skipShape(ignoreids, sp)) {
            continue;
        }
        
        Box2d extent(sp->shapec()->getExtent());
        
        if (sp->shapec()->getPointCount() > 1
            && extent.width() < xf->displayToModel(2, true)
            && extent.height() < xf->displayToModel(2, true)) { // 图形太小就跳过
            continue;
        }
        if (extent.isIntersect(wndbox)) {
            bool b1 = (needSnapHandle && snapHandle(sender, orignPt, shape, ignoreHandle,
                                                    sp, arr[0], matchpt));
            bool b2 = (needSnapPerp && snapPerp(sender, orignPt, tolPerp, shape, sp, arr[0]));
            
            if (!b1 && !b2 && needSnapNear && extent.isIntersect(snapbox)) {
                snapNear(sender, orignPt, shape, ignoreHandle, tolNear, sp, arr[0], matchpt);
            }
        }
        if (extent.isIntersect(snapbox)) {
            snapGrid(sender, orignPt, shape, ignoreHandle, sp, arr, matchpt);
        }
    }
}

// hotHandle: 绘新图时，起始步骤为-1，后续步骤>0；拖动一个或多个整体图形时为-1，拖动顶点时>=0
Point2d MgCmdManagerImpl::snapPoint(const MgMotion* sender, const Point2d& orignPt, const MgShape* shape,
                                    int hotHandle, int ignoreHandle, const int* ignoreids)
{
    const int ignoreids_tmp[2] = { shape ? shape->getID() : 0, 0 };
    if (!ignoreids) ignoreids = ignoreids_tmp;
    
    if (!shape || hotHandle >= shape->shapec()->getHandleCount()) {
        hotHandle = -1;         // 对hotHandle进行越界检查
    }
    _ptSnap = orignPt;   // 默认结果为当前触点位置
    
    const float xytol = sender->displayMmToModel("snap", "snapPointTol", 3.f);
    const float xtol = sender->displayMmToModel("snap", "snapXTol", 1.f);
    SnapItem arr[3] = {         // 设置捕捉容差和捕捉初值
        SnapItem(_ptSnap, _ptSnap, xytol),                          // XY点捕捉
        SnapItem(_ptSnap, _ptSnap, xtol),                           // X分量捕捉，竖直线
        SnapItem(_ptSnap, _ptSnap, xtol),                           // Y分量捕捉，水平线
    };
    
    if (shape && shape->getID() == 0 && hotHandle > 0               // 绘图命令中的临时图形
        && !shape->shapec()->isCurve()                              // 是线段或折线
        && !shape->shapec()->isKindOf(MgBaseRect::Type())) {        // 不是矩形或椭圆
        Point2d pt (orignPt);
        snapHV(shape->shapec()->getPoint(hotHandle - 1), pt, arr);  // 和上一个点对齐
    }
    
    Point2d pnt(-1e10f, -1e10f);                    // 当前图形的某一个顶点匹配到其他顶点pnt
    bool matchpt = (shape && shape->getID() != 0    // 拖动整个图形
                    && (hotHandle < 0 || (ignoreHandle >= 0 && ignoreHandle != hotHandle)));
    
    snapPoints(sender, orignPt, shape, ignoreHandle, ignoreids,
               arr, matchpt ? &pnt : NULL);         // 在所有图形中捕捉
    checkResult(arr);
    
    return matchpt && pnt.x > -1e8f ? pnt : _ptSnap;    // 顶点匹配优先于用触点捕捉结果
}

void MgCmdManagerImpl::checkResult(SnapItem arr[3])
{
    if (arr[0].type > 0) {                          // X和Y方向同时捕捉到一个点
        _ptSnap = arr[0].pt;                        // 结果点
        _snapBase[0] = arr[0].base;                 // 原始点
        _snapType[0] = arr[0].type;
        _snapShapeId = arr[0].shapeid;
        _snapHandle = arr[0].handleIndex;
        _snapHandleSrc = arr[0].handleIndexSrc;
    }
    else {
        _snapShapeId = 0;
        _snapHandle = -1;
        _snapHandleSrc = -1;
        
        _snapType[0] = arr[1].type;                 // 竖直方向捕捉到一个点
        if (arr[1].type > 0) {
            _ptSnap.x = arr[1].pt.x;
            _snapBase[0] = arr[1].base;
        }
        _snapType[1] = arr[2].type;                 // 水平方向捕捉到一个点
        if (arr[2].type > 0) {
            _ptSnap.y = arr[2].pt.y;
            _snapBase[1] = arr[2].base;
        }
    }
}

int MgCmdManagerImpl::getSnappedType()
{
    if (_snapType[0] >= kMgSnapPoint)
        return _snapType[0];
    if (_snapType[0] == kMgSnapGridX && _snapType[1] == kMgSnapGridY)
        return kMgSnapGrid;
    return 0;
}

int MgCmdManagerImpl::getSnappedPoint(Point2d& fromPt, Point2d& toPt)
{
    fromPt = _snapBase[0];
    toPt = _ptSnap;
    return getSnappedType();
}

bool MgCmdManagerImpl::getSnappedHandle(int& shapeid, int& handleIndex, int& handleIndexSrc)
{
    shapeid = _snapShapeId;
    handleIndex = _snapHandle;
    handleIndexSrc = _snapHandleSrc;
    return shapeid != 0;
}

void MgCmdManagerImpl::clearSnap(const MgMotion* sender)
{
    if (_snapType[0] || _snapType[1]) {
        _snapType[0] = 0;
        _snapType[1] = 0;
        sender->view->redraw();
    }
}

bool MgCmdManagerImpl::drawSnap(const MgMotion* sender, GiGraphics* gs)
{
    bool ret = false;
    
    if (sender->dragging() || !sender->view->useFinger()) {
        if (_snapType[0] >= kMgSnapGrid) {
            bool small = (_snapType[0] >= kMgSnapNearPt || _snapType[0] == kMgSnapGrid);
            Point2d pt(_snapType[0] == kMgSnapPerp ? _snapBase[0] : _ptSnap);
            GiContext ctx(-2, GiColor(0, 255, 0, 200), GiContext::kDashLine, GiColor(0, 255, 0, 64));
            
            ret = gs->drawCircle(&ctx, pt, displayMmToModel(small ? 3.f : 6.f, gs));
            gs->drawHandle(pt, kGiHandleVertex);
            if (_snapType[0] == kMgSnapPerp && _snapHandle >= 0) {
                const MgShape* sp = sender->view->shapes()->findShape(_snapShapeId);
                int n = sp ? sp->shapec()->getPointCount() : 0;
                if (n > 1) {
                    Point2d pt1(sp->shapec()->getHandlePoint(_snapHandle));
                    Point2d pt2(sp->shapec()->getHandlePoint((_snapHandle + 1) % n));
                    
                    ctx.setLineWidth(0, false);
                    gs->drawBeeline(&ctx, pt1, pt2);
                    ctx.setLineStyle(GiContext::kSolidLine);
                    if (pt1 != pt)
                        gs->drawCircle(&ctx, pt1, displayMmToModel(1.5f, gs));
                    if (pt2 != pt)
                        gs->drawCircle(&ctx, pt2, displayMmToModel(1.5f, gs));
                }
            }
        }
        else {
            GiContext ctx(0, GiColor(0, 255, 0, 200), GiContext::kDashLine, GiColor(0, 255, 0, 64));
            GiContext ctxcross(-2, GiColor(0, 255, 0, 200));
            
            if (_snapType[0] > 0) {
                if (_snapBase[0] == _ptSnap) {
                    if (_snapType[0] == kMgSnapGridX) {
                        Vector2d vec(0, displayMmToModel(15.f, gs));
                        ret = gs->drawLine(&ctxcross, _ptSnap - vec, _ptSnap + vec);
                        gs->drawCircle(&ctx, _snapBase[0], displayMmToModel(4.f, gs));
                    }
                }
                else {  // kMgSnapSameX
                    ret = gs->drawLine(&ctx, _snapBase[0], _ptSnap);
                    gs->drawCircle(&ctx, _snapBase[0], displayMmToModel(2.5f, gs));
                }
            }
            if (_snapType[1] > 0) {
                if (_snapBase[1] == _ptSnap) {
                    if (_snapType[1] == kMgSnapGridY) {
                        Vector2d vec(displayMmToModel(15.f, gs), 0);
                        ret = gs->drawLine(&ctxcross, _ptSnap - vec, _ptSnap + vec);
                        gs->drawCircle(&ctx, _snapBase[1], displayMmToModel(4.f, gs));
                    }
                }
                else {  // kMgSnapSameY
                    ret = gs->drawLine(&ctx, _snapBase[1], _ptSnap);
                    gs->drawCircle(&ctx, _snapBase[1], displayMmToModel(2.5f, gs));
                }
            }
        }
    }
    
    return ret;
}
