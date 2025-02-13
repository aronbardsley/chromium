// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_VIZ_PUBLIC_CPP_COMPOSITING_LOCAL_SURFACE_ID_STRUCT_TRAITS_H_
#define SERVICES_VIZ_PUBLIC_CPP_COMPOSITING_LOCAL_SURFACE_ID_STRUCT_TRAITS_H_

#include "components/viz/common/surfaces/local_surface_id.h"
#include "mojo/public/cpp/base/unguessable_token_mojom_traits.h"
#include "services/viz/public/interfaces/compositing/local_surface_id.mojom-shared.h"

namespace mojo {

template <>
struct StructTraits<viz::mojom::LocalSurfaceIdDataView, viz::LocalSurfaceId> {
  static uint32_t parent_sequence_number(
      const viz::LocalSurfaceId& local_surface_id) {
    return local_surface_id.parent_sequence_number();
  }

  static uint32_t child_sequence_number(
      const viz::LocalSurfaceId& local_surface_id) {
    return local_surface_id.child_sequence_number();
  }

  static const base::UnguessableToken& embed_token(
      const viz::LocalSurfaceId& local_surface_id) {
    return local_surface_id.embed_token();
  }

  static bool Read(viz::mojom::LocalSurfaceIdDataView data,
                   viz::LocalSurfaceId* out) {
    out->parent_component_.sequence_number = data.parent_sequence_number();
    out->child_sequence_number_ = data.child_sequence_number();
    return data.ReadEmbedToken(&out->parent_component_.embed_token) &&
           out->is_valid();
  }
};

}  // namespace mojo

#endif  // SERVICES_VIZ_PUBLIC_CPP_COMPOSITING_LOCAL_SURFACE_ID_STRUCT_TRAITS_H_
