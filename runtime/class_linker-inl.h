/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_RUNTIME_CLASS_LINKER_INL_H_
#define ART_RUNTIME_CLASS_LINKER_INL_H_

#include <atomic>

#include "android-base/thread_annotations.h"
#include "art_field-inl.h"
#include "art_method-inl.h"
#include "base/mutex.h"
#include "class_linker.h"
#include "class_table-inl.h"
#include "dex/dex_file.h"
#include "dex/dex_file_structs.h"
#include "gc_root-inl.h"
#include "handle_scope-inl.h"
#include "jni/jni_internal.h"
#include "mirror/class_loader.h"
#include "mirror/dex_cache-inl.h"
#include "mirror/iftable.h"
#include "mirror/object_array-inl.h"
#include "obj_ptr-inl.h"
#include "scoped_thread_state_change-inl.h"

namespace art HIDDEN {

inline ObjPtr<mirror::Class> ClassLinker::FindArrayClass(Thread* self,
                                                         ObjPtr<mirror::Class> element_class) {
  for (size_t i = 0; i < kFindArrayCacheSize; ++i) {
    // Read the cached array class once to avoid races with other threads setting it.
    ObjPtr<mirror::Class> array_class = find_array_class_cache_[i].Read();
    if (array_class != nullptr && array_class->GetComponentType() == element_class) {
      return array_class;
    }
  }
  std::string descriptor = "[";
  std::string temp;
  descriptor += element_class->GetDescriptor(&temp);
  StackHandleScope<1> hs(Thread::Current());
  Handle<mirror::ClassLoader> class_loader(hs.NewHandle(element_class->GetClassLoader()));
  ObjPtr<mirror::Class> array_class = FindClass(self, descriptor.c_str(), class_loader);
  if (array_class != nullptr) {
    // Benign races in storing array class and incrementing index.
    size_t victim_index = find_array_class_cache_next_victim_;
    find_array_class_cache_[victim_index] = GcRoot<mirror::Class>(array_class);
    find_array_class_cache_next_victim_ = (victim_index + 1) % kFindArrayCacheSize;
  } else {
    // We should have a NoClassDefFoundError.
    self->AssertPendingException();
  }
  return array_class;
}

inline ObjPtr<mirror::String> ClassLinker::ResolveString(dex::StringIndex string_idx,
                                                         ArtField* referrer) {
  Thread::PoisonObjectPointersIfDebug();
  DCHECK(!Thread::Current()->IsExceptionPending());
  ObjPtr<mirror::DexCache> dex_cache = referrer->GetDexCache();
  ObjPtr<mirror::String> resolved = dex_cache->GetResolvedString(string_idx);
  if (resolved == nullptr) {
    resolved = DoResolveString(string_idx, dex_cache);
  }
  return resolved;
}

inline ObjPtr<mirror::String> ClassLinker::ResolveString(dex::StringIndex string_idx,
                                                         ArtMethod* referrer) {
  Thread::PoisonObjectPointersIfDebug();
  DCHECK(!Thread::Current()->IsExceptionPending());
  ObjPtr<mirror::DexCache> dex_cache = referrer->GetDexCache();
  ObjPtr<mirror::String> resolved = dex_cache->GetResolvedString(string_idx);
  if (resolved == nullptr) {
    resolved = DoResolveString(string_idx, dex_cache);
  }
  return resolved;
}

inline ObjPtr<mirror::String> ClassLinker::ResolveString(dex::StringIndex string_idx,
                                                         Handle<mirror::DexCache> dex_cache) {
  Thread::PoisonObjectPointersIfDebug();
  DCHECK(!Thread::Current()->IsExceptionPending());
  ObjPtr<mirror::String> resolved = dex_cache->GetResolvedString(string_idx);
  if (resolved == nullptr) {
    resolved = DoResolveString(string_idx, dex_cache);
  }
  return resolved;
}

inline ObjPtr<mirror::String> ClassLinker::LookupString(dex::StringIndex string_idx,
                                                        ObjPtr<mirror::DexCache> dex_cache) {
  ObjPtr<mirror::String> resolved = dex_cache->GetResolvedString(string_idx);
  if (resolved == nullptr) {
    resolved = DoLookupString(string_idx, dex_cache);
  }
  return resolved;
}

inline ObjPtr<mirror::Class> ClassLinker::ResolveType(dex::TypeIndex type_idx,
                                                      ObjPtr<mirror::Class> referrer) {
  if (kObjPtrPoisoning) {
    StackHandleScope<1> hs(Thread::Current());
    HandleWrapperObjPtr<mirror::Class> referrer_wrapper = hs.NewHandleWrapper(&referrer);
    Thread::Current()->PoisonObjectPointers();
  }
  DCHECK(!Thread::Current()->IsExceptionPending());
  ObjPtr<mirror::Class> resolved_type = referrer->GetDexCache()->GetResolvedType(type_idx);
  if (resolved_type == nullptr) {
    resolved_type = DoResolveType(type_idx, referrer);
  }
  return resolved_type;
}

inline ObjPtr<mirror::Class> ClassLinker::ResolveType(dex::TypeIndex type_idx,
                                                      ArtField* referrer) {
  Thread::PoisonObjectPointersIfDebug();
  DCHECK(!Thread::Current()->IsExceptionPending());
  ObjPtr<mirror::Class> resolved_type = referrer->GetDexCache()->GetResolvedType(type_idx);
  if (UNLIKELY(resolved_type == nullptr)) {
    resolved_type = DoResolveType(type_idx, referrer);
  }
  return resolved_type;
}

inline ObjPtr<mirror::Class> ClassLinker::ResolveType(dex::TypeIndex type_idx,
                                                      ArtMethod* referrer) {
  Thread::PoisonObjectPointersIfDebug();
  DCHECK(!Thread::Current()->IsExceptionPending());
  ObjPtr<mirror::Class> resolved_type = referrer->GetDexCache()->GetResolvedType(type_idx);
  if (UNLIKELY(resolved_type == nullptr)) {
    resolved_type = DoResolveType(type_idx, referrer);
  }
  return resolved_type;
}

inline ObjPtr<mirror::Class> ClassLinker::ResolveType(dex::TypeIndex type_idx,
                                                      Handle<mirror::DexCache> dex_cache,
                                                      Handle<mirror::ClassLoader> class_loader) {
  DCHECK(dex_cache != nullptr);
  DCHECK(dex_cache->GetClassLoader() == class_loader.Get());
  Thread::PoisonObjectPointersIfDebug();
  ObjPtr<mirror::Class> resolved = dex_cache->GetResolvedType(type_idx);
  if (resolved == nullptr) {
    resolved = DoResolveType(type_idx, dex_cache, class_loader);
  }
  return resolved;
}

inline ObjPtr<mirror::Class> ClassLinker::LookupResolvedType(dex::TypeIndex type_idx,
                                                             ObjPtr<mirror::Class> referrer) {
  ObjPtr<mirror::Class> type = referrer->GetDexCache()->GetResolvedType(type_idx);
  if (type == nullptr) {
    type = DoLookupResolvedType(type_idx, referrer);
  }
  return type;
}

inline ObjPtr<mirror::Class> ClassLinker::LookupResolvedType(dex::TypeIndex type_idx,
                                                             ArtField* referrer) {
  // We do not need the read barrier for getting the DexCache for the initial resolved type
  // lookup as both from-space and to-space copies point to the same native resolved types array.
  ObjPtr<mirror::Class> type = referrer->GetDexCache()->GetResolvedType(type_idx);
  if (type == nullptr) {
    type = DoLookupResolvedType(type_idx, referrer->GetDeclaringClass());
  }
  return type;
}

inline ObjPtr<mirror::Class> ClassLinker::LookupResolvedType(dex::TypeIndex type_idx,
                                                             ArtMethod* referrer) {
  // We do not need the read barrier for getting the DexCache for the initial resolved type
  // lookup as both from-space and to-space copies point to the same native resolved types array.
  ObjPtr<mirror::Class> type = referrer->GetDexCache()->GetResolvedType(type_idx);
  if (type == nullptr) {
    type = DoLookupResolvedType(type_idx, referrer->GetDeclaringClass());
  }
  return type;
}

inline ObjPtr<mirror::Class> ClassLinker::LookupResolvedType(
    dex::TypeIndex type_idx,
    ObjPtr<mirror::DexCache> dex_cache,
    ObjPtr<mirror::ClassLoader> class_loader) {
  DCHECK(dex_cache->GetClassLoader() == class_loader);
  ObjPtr<mirror::Class> type = dex_cache->GetResolvedType(type_idx);
  if (type == nullptr) {
    type = DoLookupResolvedType(type_idx, dex_cache, class_loader);
  }
  return type;
}

template <bool kThrowOnError, typename ClassGetter>
inline bool ClassLinker::CheckInvokeClassMismatch(ObjPtr<mirror::DexCache> dex_cache,
                                                  InvokeType type,
                                                  ClassGetter class_getter) {
  switch (type) {
    case kStatic:
    case kSuper:
    case kPolymorphic:
      break;
    case kInterface: {
      // We have to check whether the method id really belongs to an interface (dex static bytecode
      // constraints A15, A16). Otherwise you must not invoke-interface on it.
      ObjPtr<mirror::Class> klass = class_getter();
      if (UNLIKELY(!klass->IsInterface())) {
        if (kThrowOnError) {
          ThrowIncompatibleClassChangeError(klass,
                                            "Found class %s, but interface was expected",
                                            klass->PrettyDescriptor().c_str());
        }
        return true;
      }
      break;
    }
    case kDirect:
      if (dex_cache->GetDexFile()->SupportsDefaultMethods()) {
        break;
      }
      FALLTHROUGH_INTENDED;
    case kVirtual: {
      // Similarly, invoke-virtual (and invoke-direct without default methods) must reference
      // a non-interface class (dex static bytecode constraint A24, A25).
      ObjPtr<mirror::Class> klass = class_getter();
      if (UNLIKELY(klass->IsInterface())) {
        if (kThrowOnError) {
          ThrowIncompatibleClassChangeError(klass,
                                            "Found interface %s, but class was expected",
                                            klass->PrettyDescriptor().c_str());
        }
        return true;
      }
      break;
    }
    default:
      LOG(FATAL) << "Unreachable - invocation type: " << type;
      UNREACHABLE();
  }
  return false;
}

template <bool kThrow>
inline bool ClassLinker::CheckInvokeClassMismatch(ObjPtr<mirror::DexCache> dex_cache,
                                                  InvokeType type,
                                                  uint32_t method_idx,
                                                  ObjPtr<mirror::ClassLoader> class_loader) {
  DCHECK(dex_cache->GetClassLoader().Ptr() == class_loader.Ptr());
  return CheckInvokeClassMismatch<kThrow>(
      dex_cache,
      type,
      [this, dex_cache, method_idx, class_loader]() REQUIRES_SHARED(Locks::mutator_lock_) {
        const dex::MethodId& method_id = dex_cache->GetDexFile()->GetMethodId(method_idx);
        ObjPtr<mirror::Class> klass =
            LookupResolvedType(method_id.class_idx_, dex_cache, class_loader);
        DCHECK(klass != nullptr) << dex_cache->GetDexFile()->PrettyMethod(method_idx);
        return klass;
      });
}

inline ArtMethod* ClassLinker::LookupResolvedMethod(uint32_t method_idx,
                                                    ObjPtr<mirror::DexCache> dex_cache,
                                                    ObjPtr<mirror::ClassLoader> class_loader) {
  DCHECK(dex_cache->GetClassLoader() == class_loader);
  ArtMethod* resolved = dex_cache->GetResolvedMethod(method_idx);
  if (resolved == nullptr) {
    const DexFile& dex_file = *dex_cache->GetDexFile();
    const dex::MethodId& method_id = dex_file.GetMethodId(method_idx);
    ObjPtr<mirror::Class> klass = LookupResolvedType(method_id.class_idx_, dex_cache, class_loader);
    if (klass != nullptr) {
      resolved = FindResolvedMethod(klass, dex_cache, class_loader, method_idx);
    }
  }
  return resolved;
}

template <ClassLinker::ResolveMode kResolveMode>
inline ArtMethod* ClassLinker::ResolveMethod(Thread* self,
                                             uint32_t method_idx,
                                             ArtMethod* referrer,
                                             InvokeType type) {
  DCHECK(referrer != nullptr);
  DCHECK_IMPLIES(referrer->IsProxyMethod(), referrer->IsConstructor());

  Thread::PoisonObjectPointersIfDebug();
  // Fast path: no checks and in the dex cache.
  if (kResolveMode == ResolveMode::kNoChecks) {
    ArtMethod* resolved_method = referrer->GetDexCache()->GetResolvedMethod(method_idx);
    if (resolved_method != nullptr) {
      DCHECK(!resolved_method->IsRuntimeMethod());
      return resolved_method;
    }
  }

  // For a Proxy constructor, we need to do the lookup in the context of the original method
  // from where it steals the code.
  referrer = referrer->GetInterfaceMethodIfProxy(image_pointer_size_);
  StackHandleScope<2> hs(self);
  Handle<mirror::DexCache> dex_cache(hs.NewHandle(referrer->GetDexCache()));
  Handle<mirror::ClassLoader> class_loader(
      hs.NewHandle(referrer->GetDeclaringClass()->GetClassLoader()));
  return ResolveMethod<kResolveMode>(method_idx, dex_cache, class_loader, referrer, type);
}

template <ClassLinker::ResolveMode kResolveMode>
inline ArtMethod* ClassLinker::ResolveMethod(uint32_t method_idx,
                                             Handle<mirror::DexCache> dex_cache,
                                             Handle<mirror::ClassLoader> class_loader,
                                             ArtMethod* referrer,
                                             InvokeType type) {
  DCHECK(dex_cache != nullptr);
  DCHECK(dex_cache->GetClassLoader() == class_loader.Get());
  DCHECK(!Thread::Current()->IsExceptionPending()) << Thread::Current()->GetException()->Dump();
  DCHECK(referrer == nullptr || !referrer->IsProxyMethod());

  // Check for hit in the dex cache.
  ArtMethod* resolved = dex_cache->GetResolvedMethod(method_idx);
  Thread::PoisonObjectPointersIfDebug();
  DCHECK(resolved == nullptr || !resolved->IsRuntimeMethod());
  bool valid_dex_cache_method = resolved != nullptr;
  if (kResolveMode == ResolveMode::kNoChecks && valid_dex_cache_method) {
    // We have a valid method from the DexCache and no checks to perform.
    DCHECK(resolved->GetDeclaringClassUnchecked() != nullptr) << resolved->GetDexMethodIndex();
    return resolved;
  }
  const DexFile& dex_file = *dex_cache->GetDexFile();
  const dex::MethodId& method_id = dex_file.GetMethodId(method_idx);
  ObjPtr<mirror::Class> klass = nullptr;
  if (valid_dex_cache_method) {
    // We have a valid method from the DexCache but we need to perform ICCE and IAE checks.
    DCHECK(resolved->GetDeclaringClassUnchecked() != nullptr) << resolved->GetDexMethodIndex();
    klass = LookupResolvedType(method_id.class_idx_, dex_cache.Get(), class_loader.Get());
    if (UNLIKELY(klass == nullptr)) {
      // We normaly should not end up here. However the verifier currently doesn't guarantee
      // the invariant of having the klass in the class table. b/73760543
      klass = ResolveType(method_id.class_idx_, dex_cache, class_loader);
      if (klass == nullptr) {
        // This can only happen if the current thread is not allowed to load
        // classes.
        DCHECK(!Thread::Current()->CanLoadClasses());
        DCHECK(Thread::Current()->IsExceptionPending());
        return nullptr;
      }
    }
  } else {
    // The method was not in the DexCache, resolve the declaring class.
    klass = ResolveType(method_id.class_idx_, dex_cache, class_loader);
    if (klass == nullptr) {
      DCHECK(Thread::Current()->IsExceptionPending());
      return nullptr;
    }
    // Look for the method again in case the type resolution updated the cache.
    resolved = dex_cache->GetResolvedMethod(method_idx);
    if (kResolveMode == ResolveMode::kNoChecks && resolved != nullptr) {
      return resolved;
    }
  }

  // Check if the invoke type matches the class type.
  if (kResolveMode == ResolveMode::kCheckICCEAndIAE &&
      CheckInvokeClassMismatch</* kThrow= */ true>(
          dex_cache.Get(), type, [klass]() { return klass; })) {
    DCHECK(Thread::Current()->IsExceptionPending());
    return nullptr;
  }

  if (!valid_dex_cache_method) {
    resolved = FindResolvedMethod(klass, dex_cache.Get(), class_loader.Get(), method_idx);
  }

  // Note: We can check for IllegalAccessError only if we have a referrer.
  if (kResolveMode == ResolveMode::kCheckICCEAndIAE && resolved != nullptr && referrer != nullptr) {
    ObjPtr<mirror::Class> methods_class = resolved->GetDeclaringClass();
    ObjPtr<mirror::Class> referring_class = referrer->GetDeclaringClass();
    if (UNLIKELY(!referring_class->CanAccess(methods_class))) {
      // The referrer class can't access the method's declaring class but may still be able
      // to access the method if the MethodId specifies an accessible subclass of the declaring
      // class rather than the declaring class itself.
      if (UNLIKELY(!referring_class->CanAccess(klass))) {
        ThrowIllegalAccessErrorClassForMethodDispatch(referring_class,
                                                      klass,
                                                      resolved,
                                                      type);
        return nullptr;
      }
    }
    if (UNLIKELY(!referring_class->CanAccessMember(methods_class, resolved->GetAccessFlags()))) {
      ThrowIllegalAccessErrorMethod(referring_class, resolved);
      return nullptr;
    }
  }

  // If we found a method, check for incompatible class changes.
  if (LIKELY(resolved != nullptr) &&
      LIKELY(kResolveMode == ResolveMode::kNoChecks ||
             !resolved->CheckIncompatibleClassChange(type))) {
    return resolved;
  }

  // If we had a method, or if we can find one with another lookup type,
  // it's an incompatible-class-change error.
  if (resolved == nullptr) {
    resolved = FindIncompatibleMethod(klass, dex_cache.Get(), class_loader.Get(), method_idx);
  }
  if (resolved != nullptr) {
    ThrowIncompatibleClassChangeError(type, resolved->GetInvokeType(), resolved, referrer);
  } else {
    // We failed to find the method (using all lookup types), so throw a NoSuchMethodError.
    const char* name = dex_file.StringDataByIdx(method_id.name_idx_);
    const Signature signature = dex_file.GetMethodSignature(method_id);
    ThrowNoSuchMethodError(type, klass, name, signature);
  }
  Thread::Current()->AssertPendingException();
  return nullptr;
}

inline ArtField* ClassLinker::LookupResolvedField(uint32_t field_idx,
                                                  ArtMethod* referrer,
                                                  bool is_static) {
  ObjPtr<mirror::DexCache> dex_cache = referrer->GetDexCache();
  ArtField* field = dex_cache->GetResolvedField(field_idx);
  if (field == nullptr) {
    referrer = referrer->GetInterfaceMethodIfProxy(image_pointer_size_);
    ObjPtr<mirror::ClassLoader> class_loader = referrer->GetDeclaringClass()->GetClassLoader();
    field = LookupResolvedField(field_idx, dex_cache, class_loader, is_static);
  }
  return field;
}

inline ArtField* ClassLinker::ResolveField(uint32_t field_idx,
                                           ArtMethod* referrer,
                                           bool is_static) {
  Thread::PoisonObjectPointersIfDebug();
  ObjPtr<mirror::DexCache> dex_cache = referrer->GetDexCache();
  ArtField* resolved_field = dex_cache->GetResolvedField(field_idx);
  if (UNLIKELY(resolved_field == nullptr)) {
    StackHandleScope<2> hs(Thread::Current());
    referrer = referrer->GetInterfaceMethodIfProxy(image_pointer_size_);
    ObjPtr<mirror::Class> referring_class = referrer->GetDeclaringClass();
    Handle<mirror::DexCache> h_dex_cache(hs.NewHandle(dex_cache));
    Handle<mirror::ClassLoader> class_loader(hs.NewHandle(referring_class->GetClassLoader()));
    resolved_field = ResolveField(field_idx, h_dex_cache, class_loader, is_static);
    // Note: We cannot check here to see whether we added the field to the cache. The type
    //       might be an erroneous class, which results in it being hidden from us.
  }
  return resolved_field;
}

inline ArtField* ClassLinker::ResolveField(uint32_t field_idx,
                                           Handle<mirror::DexCache> dex_cache,
                                           Handle<mirror::ClassLoader> class_loader,
                                           bool is_static) {
  DCHECK(dex_cache != nullptr);
  DCHECK(dex_cache->GetClassLoader().Ptr() == class_loader.Get());
  DCHECK(!Thread::Current()->IsExceptionPending()) << Thread::Current()->GetException()->Dump();
  ArtField* resolved = dex_cache->GetResolvedField(field_idx);
  Thread::PoisonObjectPointersIfDebug();
  if (resolved != nullptr) {
    return resolved;
  }
  const DexFile& dex_file = *dex_cache->GetDexFile();
  const dex::FieldId& field_id = dex_file.GetFieldId(field_idx);
  ObjPtr<mirror::Class> klass = ResolveType(field_id.class_idx_, dex_cache, class_loader);
  if (klass == nullptr) {
    DCHECK(Thread::Current()->IsExceptionPending());
    return nullptr;
  }

  // Look for the field again in case the type resolution updated the cache.
  resolved = dex_cache->GetResolvedField(field_idx);
  if (resolved != nullptr) {
    return resolved;
  }

  resolved = FindResolvedField(klass, dex_cache.Get(), class_loader.Get(), field_idx, is_static);
  if (resolved == nullptr) {
    const char* name = dex_file.GetFieldName(field_id);
    const char* type = dex_file.GetFieldTypeDescriptor(field_id);
    ThrowNoSuchFieldError(is_static ? "static " : "instance ", klass, type, name);
  }
  return resolved;
}

template <typename Visitor>
inline void ClassLinker::VisitBootClasses(Visitor* visitor) {
  boot_class_table_->Visit(*visitor);
}

template <class Visitor>
inline void ClassLinker::VisitClassTables(const Visitor& visitor) {
  Thread* const self = Thread::Current();
  WriterMutexLock mu(self, *Locks::classlinker_classes_lock_);
  for (const ClassLoaderData& data : class_loaders_) {
    if (data.class_table != nullptr) {
      visitor(data.class_table);
    }
  }
}

template <ReadBarrierOption kReadBarrierOption>
inline ObjPtr<mirror::ObjectArray<mirror::Class>> ClassLinker::GetClassRoots() {
  ObjPtr<mirror::ObjectArray<mirror::Class>> class_roots =
      class_roots_.Read<kReadBarrierOption>();
  DCHECK(class_roots != nullptr);
  return class_roots;
}

template <typename Visitor>
void ClassLinker::VisitKnownDexFiles(Thread* self, Visitor visitor) {
  ReaderMutexLock rmu(self, *Locks::dex_lock_);
  std::for_each(dex_caches_.begin(),
                dex_caches_.end(),
                [&](const auto& entry) REQUIRES(Locks::mutator_lock_) {
                  visitor(/*dex_file=*/entry.first);
                });
}

}  // namespace art

#endif  // ART_RUNTIME_CLASS_LINKER_INL_H_
