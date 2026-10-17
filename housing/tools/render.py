#!/usr/bin/env python3
"""
VTK off-screen rendering helpers for the housing previews (used by render_lite.py):
  actor(shape, colour, offset)   a CadQuery shape as a coloured VTK actor
  render(actors, path, ...)      one PNG from a camera position
"""
import vtk

COL = {"housing": (0.52, 0.55, 0.60), "dark": (0.34, 0.36, 0.40), "pcb": (0.13, 0.45, 0.25),
       "cells": (0.86, 0.62, 0.74), "display": (0.62, 0.70, 0.78), "batt": (0.25, 0.42, 0.85),
       "cap": (0.88, 0.42, 0.16), "bed": (0.82, 0.82, 0.82)}


def polydata(shape, tol=0.15):
    verts, tris = shape.val().tessellate(tol, 0.2)
    pts = vtk.vtkPoints()
    for p in verts:
        pts.InsertNextPoint(p.x, p.y, p.z)
    cells = vtk.vtkCellArray()
    for t in tris:
        cells.InsertNextCell(3)
        for i in t:
            cells.InsertCellPoint(i)
    pd = vtk.vtkPolyData()
    pd.SetPoints(pts)
    pd.SetPolys(cells)
    n = vtk.vtkPolyDataNormals()
    n.SetInputData(pd)
    n.SetFeatureAngle(35)
    n.Update()
    return n.GetOutput()


def actor(shape, color, offset=(0, 0, 0)):
    m = vtk.vtkPolyDataMapper()
    m.SetInputData(polydata(shape))
    a = vtk.vtkActor()
    a.SetMapper(m)
    pr = a.GetProperty()
    pr.SetColor(*COL[color])
    pr.SetAmbient(0.35)
    pr.SetDiffuse(0.75)
    pr.SetSpecular(0.25)
    pr.SetSpecularPower(25)
    a.SetPosition(*offset)
    return a


def render(actors, path, cam_pos, focal, view_up=(0, 1, 0), size=(1600, 900), zoom=1.0, title=None):
    r = vtk.vtkRenderer()
    r.SetBackground(1, 1, 1)
    for a in actors:
        r.AddActor(a)
    w = vtk.vtkRenderWindow()
    w.SetOffScreenRendering(1)
    w.SetSize(*size)
    w.SetMultiSamples(8)
    w.AddRenderer(r)
    cam = r.GetActiveCamera()
    cam.SetPosition(*cam_pos)
    cam.SetFocalPoint(*focal)
    cam.SetViewUp(*view_up)
    r.ResetCamera()
    cam.Zoom(zoom)
    light = vtk.vtkLight()
    light.SetLightTypeToCameraLight()
    light.SetPosition(0.6, 1.0, 1.0)
    light.SetIntensity(0.55)
    r.AddLight(light)
    if title:
        t = vtk.vtkTextActor()
        t.SetInput(title)
        t.GetTextProperty().SetFontSize(26)
        t.GetTextProperty().SetColor(0.15, 0.15, 0.15)
        t.SetPosition(20, size[1] - 45)
        r.AddViewProp(t)
    w.Render()
    f = vtk.vtkWindowToImageFilter()
    f.SetInput(w)
    f.Update()
    wr = vtk.vtkPNGWriter()
    wr.SetFileName(str(path))
    wr.SetInputConnection(f.GetOutputPort())
    wr.Write()
