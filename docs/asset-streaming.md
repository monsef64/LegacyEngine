# Scene asset streaming

The Vulkan scene renderer requests model meshes and material textures only when
an active rendered object references them. A bounded background worker reads and
decompresses `.lemesh` files and a second worker reads and decodes material
images. Jobs own their input paths and do not access the ECS, renderer caches or
Vulkan device. Pending meshes are not drawn; pending textures use the renderer's
default texture. Cached assets are reused by Scene, Game and editor previews.

Each worker queues at most two jobs, and each cache retains at most two pending
results. Project switches and model revisions discard pending results; queued
work is canceled. A job already in progress may finish, but cannot publish into
the new project. The render thread uploads completed resources to Vulkan, since
queue/device operations require synchronization. Uploads can still cause a
frame-time spike. Failed loads are reported and retried at most once per second.

This is not yet streaming for every asset consumer: preview bounds/model
metadata, standalone asset previews, skyboxes, and physics mesh-collider loading
may still perform synchronous reads; animated mesh skinning and Vulkan uploads
also remain on the render thread. CPU image and mesh data remain bounded by their
existing per-asset validation limits, but a model may reference multiple parts.
