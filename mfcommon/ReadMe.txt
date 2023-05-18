========================================================================
    MEDIA FOUNDATION SAMPLE HELPER FUNCTIONS
========================================================================

General Purpose Helpers
-----------------------

AsyncCB.h           Routes IMFAsyncCallback::Invoke calls to a class method on the 
                    parent class.

BufferLock.h        Locks a video buffer that might or might not support IMF2DBuffer.
                                        
ClassFactory.h      ClassFactory class: Implements IClassFactory.

                    BaseObject class: Base class for objects created by the class factory.

                    RefCountedObject class: Base class for implementing AddRef and Release.

Common.h            Main header for helper files.

Critsec.h           Critical section wrapper.
                    
GrowArray.h         Resizable array.

LinkList.h          List class: Linked list.

                    ComPtrList class: Linked list of COM pointers.

Logging.h           Debug logging.

MediaType.h         Wrappers for IMFMediaType.

Mfutils.h           Utilility functions.

PropVar.h           PROPVARIANT helper.

Registry.h          Functions to create registry entries for COM objects.

SmartPtr.h          Smart COM pointer.

TinyMap.h           Simple map class for storing key/value pairs.

Trace.h             Functions to output names of MF data types (enums, etc).
   
  
Copyright (c) Microsoft Corporation. All rights reserved.