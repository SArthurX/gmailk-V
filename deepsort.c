CVI_S32 CVI_TDL_DeepSORT_Face(const cvitdl_handle_t handle, cvtdl_face_t *face,
                              cvtdl_tracker_t *tracker) {
  cvitdl_context_t *ctx = static_cast<cvitdl_context_t *>(handle);
  DeepSORT *ds_tracker = ctx->ds_tracker;
  if (ds_tracker == nullptr) {
    LOGE("Please initialize DeepSORT first.\n");
    return CVI_TDL_FAILURE;
  }
  return ctx->ds_tracker->track(face, tracker);
}
CVI_S32 CVI_TDL_DeepSORT_Obj(const cvitdl_handle_t handle, cvtdl_object_t *obj,
                             cvtdl_tracker_t *tracker, bool use_reid) {
  cvitdl_context_t *ctx = static_cast<cvitdl_context_t *>(handle);
  DeepSORT *ds_tracker = ctx->ds_tracker;
  if (ds_tracker == nullptr) {
    LOGE("Please initialize DeepSORT first.\n");
    return CVI_TDL_FAILURE;
  }
  return ctx->ds_tracker->track(obj, tracker, use_reid);
}

CVI_S32 CVI_TDL_DeepSORT_Byte(const cvitdl_handle_t handle, cvtdl_object_t *obj,
                              cvtdl_tracker_t *tracker, bool use_reid) {
  cvitdl_context_t *ctx = static_cast<cvitdl_context_t *>(handle);
  DeepSORT *ds_tracker = ctx->ds_tracker;
  if (ds_tracker == nullptr) {
    LOGE("Please initialize DeepSORT first.\n");
    return CVI_TDL_FAILURE;
  }
  return ctx->ds_tracker->byte_track(obj, tracker, use_reid);
}
CVI_S32 DeepSORT::byte_track(cvtdl_object_t *obj, cvtdl_tracker_t *tracker, bool use_reid,
                             float low_score, float high_score) {
  /** statistic what classes ID in bbox and tracker,
   *  and counting bbox number for each class */

  CVI_S32 ret = CVI_TDL_SUCCESS;
  std::map<int, int> class_bbox_counter;
  std::set<int> class_ids_bbox;
  std::set<int> class_ids_trackers;
  for (uint32_t i = 0; i < obj->size; i++) {
    auto iter = class_bbox_counter.find(obj->info[i].classes);
    if (iter != class_bbox_counter.end()) {
      iter->second += 1;
    } else {
      class_bbox_counter.insert(std::pair<int, int>(obj->info[i].classes, 1));
      class_ids_bbox.insert(obj->info[i].classes);
    }
  }

  for (size_t j = 0; j < k_trackers.size(); j++) {
    class_ids_trackers.insert(k_trackers[j].class_id);
  }

  CVI_TDL_MemAlloc(obj->size, tracker);

  /** run tracking function for each class ID in bbox
   */
  for (auto const &e : class_bbox_counter) {
    /** pick up all bboxes and features data for this class ID
     */
    std::vector<BBOX> high_bboxes;
    std::vector<BBOX> low_bboxes;

    std::vector<FEATURE> high_features;
    std::vector<FEATURE> low_features;

    std::vector<int> idx_table;
    for (uint32_t i = 0; i < obj->size; i++) {
      if (obj->info[i].classes == e.first) {
        if (obj->info[i].bbox.score > high_score) {
          idx_table.push_back(static_cast<int>(i));
          BBOX bbox_;
          uint32_t feature_size = obj->info[i].feature.size;
          FEATURE feature_(feature_size);
          bbox_(0, 0) = obj->info[i].bbox.x1;
          bbox_(0, 1) = obj->info[i].bbox.y1;
          bbox_(0, 2) = obj->info[i].bbox.x2 - obj->info[i].bbox.x1;
          bbox_(0, 3) = obj->info[i].bbox.y2 - obj->info[i].bbox.y1;
          if (obj->info[i].feature.type != TYPE_INT8) {
            LOGE("Feature Type not support now.\n");
            return CVI_TDL_ERR_INVALID_ARGS;
          }
          int type_size = getFeatureTypeSize(obj->info[i].feature.type);
          for (uint32_t d = 0; d < feature_size; d++) {
            feature_(d) = static_cast<float>(obj->info[i].feature.ptr[d * type_size]);
          }
          high_bboxes.push_back(bbox_);
          high_features.push_back(feature_);
        } else if (obj->info[i].bbox.score > low_score) {
          idx_table.push_back(static_cast<int>(i));
          BBOX bbox_;
          uint32_t feature_size = obj->info[i].feature.size;
          FEATURE feature_(feature_size);
          bbox_(0, 0) = obj->info[i].bbox.x1;
          bbox_(0, 1) = obj->info[i].bbox.y1;
          bbox_(0, 2) = obj->info[i].bbox.x2 - obj->info[i].bbox.x1;
          bbox_(0, 3) = obj->info[i].bbox.y2 - obj->info[i].bbox.y1;
          if (obj->info[i].feature.type != TYPE_INT8) {
            LOGE("Feature Type not support now.\n");
            return CVI_TDL_ERR_INVALID_ARGS;
          }
          int type_size = getFeatureTypeSize(obj->info[i].feature.type);
          for (uint32_t d = 0; d < feature_size; d++) {
            feature_(d) = static_cast<float>(obj->info[i].feature.ptr[d * type_size]);
          }
          low_bboxes.push_back(bbox_);
          low_features.push_back(feature_);
        }
      }
    }
    assert(idx_table.size() == static_cast<size_t>(e.second));

    /** run tracking function
     *    - ReID flag is only avaliable for PERSON now.
     */
    Tracking_Result high_result_(high_bboxes.size());
    Tracking_Result low_result_(low_bboxes.size());
    ret = track_impl(high_result_, low_result_, high_bboxes, low_bboxes, high_features,
                     low_features, 0.3, e.first, use_reid && (e.first == CVI_TDL_DET_TYPE_PERSON));
    if (CVI_TDL_SUCCESS != ret) {
      return ret;
    }
    /* high bbox update state */
    for (size_t i = 0; i < high_result_.size(); i++) {
      int idx = idx_table[i];
      bool &matched = std::get<0>(high_result_[i]);
      uint64_t &t_id = std::get<1>(high_result_[i]);
      k_tracker_state_e &t_state = std::get<2>(high_result_[i]);
      BBOX &t_bbox = std::get<3>(high_result_[i]);
      if (!matched) {
        tracker->info[idx].state = cvtdl_trk_state_type_t::CVI_TRACKER_NEW;
      } else if (t_state == k_tracker_state_e::PROBATION) {
        tracker->info[idx].state = cvtdl_trk_state_type_t::CVI_TRACKER_UNSTABLE;
      } else if (t_state == k_tracker_state_e::ACCREDITATION) {
        tracker->info[idx].state = cvtdl_trk_state_type_t::CVI_TRACKER_STABLE;
      } else {
        LOGE("Tracker State Unknow.\n");
        return CVI_TDL_ERR_INVALID_ARGS;
      }
      tracker->info[idx].bbox.x1 = t_bbox(0);
      tracker->info[idx].bbox.y1 = t_bbox(1);
      tracker->info[idx].bbox.x2 = t_bbox(0) + t_bbox(2);
      tracker->info[idx].bbox.y2 = t_bbox(1) + t_bbox(3);
      obj->info[idx].unique_id = t_id;
      tracker->info[idx].id = t_id;
    }
    /* low bbox update state */
    for (size_t i = 0; i < low_result_.size(); i++) {
      int i_ = i + high_result_.size();
      int idx = idx_table[i_];
      bool &matched = std::get<0>(low_result_[i]);
      uint64_t &t_id = std::get<1>(low_result_[i]);
      k_tracker_state_e &t_state = std::get<2>(low_result_[i]);
      BBOX &t_bbox = std::get<3>(low_result_[i]);
      if (matched) {
        if (t_state == k_tracker_state_e::PROBATION) {
          tracker->info[idx].state = cvtdl_trk_state_type_t::CVI_TRACKER_UNSTABLE;
        } else if (t_state == k_tracker_state_e::ACCREDITATION) {
          tracker->info[idx].state = cvtdl_trk_state_type_t::CVI_TRACKER_STABLE;
        } else {
          LOGE("Tracker State Unknow.\n");
          return CVI_TDL_ERR_INVALID_ARGS;
        }
        tracker->info[idx].bbox.x1 = t_bbox(0);
        tracker->info[idx].bbox.y1 = t_bbox(1);
        tracker->info[idx].bbox.x2 = t_bbox(0) + t_bbox(2);
        tracker->info[idx].bbox.y2 = t_bbox(1) + t_bbox(3);
        obj->info[idx].unique_id = t_id;
        tracker->info[idx].id = t_id;
      } else {
        obj->info[idx].unique_id = 0;
        tracker->info[idx].id = 0;
      }
    }
  }
  /** update tracker state even though there is no relative bbox (by class ID) at this time.
   */
  for (std::set<int>::iterator it = class_ids_trackers.begin(); it != class_ids_trackers.end();
       ++it) {
    if (class_ids_bbox.find(*it) == class_ids_bbox.end()) {
      std::vector<BBOX> bboxes;
      std::vector<FEATURE> features;
      Tracking_Result result_;
      if (CVI_SUCCESS != track_impl(result_, bboxes, features, 0.3, *it, use_reid)) {
        return CVI_TDL_FAILURE;
      }
    }
  }
  return CVI_TDL_SUCCESS;
}
CVI_S32 DeepSORT::track(cvtdl_object_t *obj, cvtdl_tracker_t *tracker, bool use_reid) {
  /** statistic what classes ID in bbox and tracker,
   *  and counting bbox number for each class */

  CVI_S32 ret = CVI_TDL_SUCCESS;
  std::map<int, int> class_bbox_counter;
  std::set<int> class_ids_bbox;
  std::set<int> class_ids_trackers;
  for (uint32_t i = 0; i < obj->size; i++) {
    auto iter = class_bbox_counter.find(obj->info[i].classes);
    if (iter != class_bbox_counter.end()) {
      iter->second += 1;
    } else {
      class_bbox_counter.insert(std::pair<int, int>(obj->info[i].classes, 1));
      class_ids_bbox.insert(obj->info[i].classes);
    }
  }

  for (size_t j = 0; j < k_trackers.size(); j++) {
    class_ids_trackers.insert(k_trackers[j].class_id);
  }

  CVI_TDL_MemAlloc(obj->size, tracker);

  /** run tracking function for each class ID in bbox
   */
  for (auto const &e : class_bbox_counter) {
    /** pick up all bboxes and features data for this class ID
     */
    std::vector<BBOX> bboxes;
    std::vector<FEATURE> features;
    std::vector<int> idx_table;
    for (uint32_t i = 0; i < obj->size; i++) {
      if (obj->info[i].classes == e.first) {
        idx_table.push_back(static_cast<int>(i));
        BBOX bbox_;
        uint32_t feature_size = obj->info[i].feature.size;
        FEATURE feature_(feature_size);
        bbox_(0, 0) = obj->info[i].bbox.x1;
        bbox_(0, 1) = obj->info[i].bbox.y1;
        bbox_(0, 2) = obj->info[i].bbox.x2 - obj->info[i].bbox.x1;
        bbox_(0, 3) = obj->info[i].bbox.y2 - obj->info[i].bbox.y1;
        if (obj->info[i].feature.type != TYPE_INT8) {
          LOGE("Feature Type not support now.\n");
          return CVI_TDL_ERR_INVALID_ARGS;
        }
        int type_size = getFeatureTypeSize(obj->info[i].feature.type);
        for (uint32_t d = 0; d < feature_size; d++) {
          feature_(d) = static_cast<float>(obj->info[i].feature.ptr[d * type_size]);
        }
        bboxes.push_back(bbox_);
        features.push_back(feature_);
      }
    }
    assert(idx_table.size() == static_cast<size_t>(e.second));

    /** run tracking function
     *    - ReID flag is only avaliable for PERSON now.
     */
    Tracking_Result result_(bboxes.size());
    ret = track_impl(result_, bboxes, features, 0.3, e.first,
                     use_reid && (e.first == CVI_TDL_DET_TYPE_PERSON));
    if (CVI_TDL_SUCCESS != ret) {
      return ret;
    }

    for (size_t i = 0; i < result_.size(); i++) {
      int idx = idx_table[i];
      bool &matched = std::get<0>(result_[i]);
      uint64_t &t_id = std::get<1>(result_[i]);
      k_tracker_state_e &t_state = std::get<2>(result_[i]);
      BBOX &t_bbox = std::get<3>(result_[i]);
      if (!matched) {
        tracker->info[idx].state = cvtdl_trk_state_type_t::CVI_TRACKER_NEW;
        obj->info[i].track_state = cvtdl_trk_state_type_t::CVI_TRACKER_NEW;
      } else if (t_state == k_tracker_state_e::PROBATION) {
        tracker->info[idx].state = cvtdl_trk_state_type_t::CVI_TRACKER_UNSTABLE;
        obj->info[i].track_state = cvtdl_trk_state_type_t::CVI_TRACKER_UNSTABLE;
      } else if (t_state == k_tracker_state_e::ACCREDITATION) {
        tracker->info[idx].state = cvtdl_trk_state_type_t::CVI_TRACKER_STABLE;
        obj->info[i].track_state = cvtdl_trk_state_type_t::CVI_TRACKER_STABLE;
      } else {
        LOGE("Tracker State Unknow.\n");
        return CVI_TDL_ERR_INVALID_ARGS;
      }
      tracker->info[idx].bbox.x1 = t_bbox(0);
      tracker->info[idx].bbox.y1 = t_bbox(1);
      tracker->info[idx].bbox.x2 = t_bbox(0) + t_bbox(2);
      tracker->info[idx].bbox.y2 = t_bbox(1) + t_bbox(3);
      obj->info[idx].unique_id = t_id;
      tracker->info[idx].id = t_id;
    }
  }

  /** update tracker state even though there is no relative bbox (by class ID) at this time.
   */
  for (std::set<int>::iterator it = class_ids_trackers.begin(); it != class_ids_trackers.end();
       ++it) {
    if (class_ids_bbox.find(*it) == class_ids_bbox.end()) {
      std::vector<BBOX> bboxes;
      std::vector<FEATURE> features;
      Tracking_Result result_;
      if (CVI_SUCCESS != track_impl(result_, bboxes, features, 0.3, *it, use_reid)) {
        return CVI_TDL_FAILURE;
      }
    }
  }

  return CVI_TDL_SUCCESS;
}

CVI_S32 DeepSORT::track(cvtdl_face_t *face, cvtdl_tracker_t *tracker) {
#ifdef DEBUG_TRACK
  std::cout << "start to track,face num:" << face->size << std::endl;
  show_INFO_KalmanTrackers();
#endif
  CVI_S32 ret = CVI_TDL_SUCCESS;
  std::vector<BBOX> bboxes;
  std::vector<FEATURE> features;
  uint32_t bbox_num = face->size;

  bool use_reid = true;
  for (uint32_t i = 0; i < bbox_num; i++) {
    if (face->info[i].feature.size == 0 ||
        face->info[0].feature.size != face->info[i].feature.size) {
      use_reid = false;
      break;
    }
  }
  for (uint32_t i = 0; i < bbox_num; i++) {
    BBOX bbox_;
    bbox_(0, 0) = face->info[i].bbox.x1;
    bbox_(0, 1) = face->info[i].bbox.y1;
    bbox_(0, 2) = face->info[i].bbox.x2 - face->info[i].bbox.x1;
    bbox_(0, 3) = face->info[i].bbox.y2 - face->info[i].bbox.y1;
    bboxes.push_back(bbox_);
    uint32_t feature_size = (use_reid) ? face->info[i].feature.size : 0;
    FEATURE feature_(feature_size);
    if (use_reid) {
      if (face->info[i].feature.type != TYPE_INT8) {
        LOGE("Feature Type not support now.\n");
        return CVI_TDL_FAILURE;
      }
      int type_size = getFeatureTypeSize(face->info[i].feature.type);
      for (uint32_t d = 0; d < feature_size; d++) {
        feature_(d) = static_cast<float>(face->info[i].feature.ptr[d * type_size]);
      }
    }
    features.push_back(feature_);
  }

  float *bbox_quality = (float *)malloc(sizeof(float) * bbox_num);
  for (uint32_t i = 0; i < bbox_num; i++) {
    bbox_quality[i] = face->info[i].face_quality;
    if (face->info[i].pose_score == 0) {
      bbox_quality[i] = 0;
    }
  }

  Tracking_Result result_(bboxes.size());
  ret = track_impl(result_, bboxes, features, 0.1, -1, use_reid, bbox_quality);
  if (CVI_TDL_SUCCESS != ret) {
    free(bbox_quality);
    printf("ERROR\n");
    return ret;
  }
  free(bbox_quality);

  CVI_TDL_MemAlloc(k_trackers.size(), tracker);
  for (uint32_t i = 0; i < k_trackers.size(); i++) {
    memset(&tracker->info[i], 0, sizeof(tracker->info[i]));
    auto *p_track = &k_trackers[i];
    if (p_track->ages_ == 1) {
      tracker->info[i].state = cvtdl_trk_state_type_t::CVI_TRACKER_NEW;
    } else if (p_track->tracker_state == k_tracker_state_e::PROBATION) {
      tracker->info[i].state = cvtdl_trk_state_type_t::CVI_TRACKER_UNSTABLE;
    } else if (p_track->tracker_state == k_tracker_state_e::ACCREDITATION) {
      tracker->info[i].state = cvtdl_trk_state_type_t::CVI_TRACKER_STABLE;
    } else {
      LOGE("Tracker State Unknow.\n");
      printf("track unknown type error\n");
      continue;
    }
    BBOX t_bbox = p_track->getBBox_TLWH();
    tracker->info[i].bbox.x1 = t_bbox(0);
    tracker->info[i].bbox.y1 = t_bbox(1);
    tracker->info[i].bbox.x2 = t_bbox(0) + t_bbox(2);
    tracker->info[i].bbox.y2 = t_bbox(1) + t_bbox(3);
    tracker->info[i].id = p_track->id;
    tracker->info[i].out_num = p_track->out_nums;
  }

  assert(result_.size() == static_cast<size_t>(bbox_num));
  for (size_t i = 0; i < result_.size(); i++) {
    bool &matched = std::get<0>(result_[i]);
    uint64_t &t_id = std::get<1>(result_[i]);
    k_tracker_state_e &t_state = std::get<2>(result_[i]);
    BBOX &t_bbox = std::get<3>(result_[i]);
    if (!matched) {
      tracker->info[i].state = cvtdl_trk_state_type_t::CVI_TRACKER_NEW;
      face->info[i].track_state = cvtdl_trk_state_type_t::CVI_TRACKER_NEW;
    } else if (t_state == k_tracker_state_e::PROBATION) {
      tracker->info[i].state = cvtdl_trk_state_type_t::CVI_TRACKER_UNSTABLE;
      face->info[i].track_state = cvtdl_trk_state_type_t::CVI_TRACKER_UNSTABLE;
    } else if (t_state == k_tracker_state_e::ACCREDITATION) {
      tracker->info[i].state = cvtdl_trk_state_type_t::CVI_TRACKER_STABLE;
      face->info[i].track_state = cvtdl_trk_state_type_t::CVI_TRACKER_STABLE;
    } else {
      LOGE("Tracker State Unknow.\n");
      return CVI_TDL_ERR_INVALID_ARGS;
    }
    tracker->info[i].bbox.x1 = t_bbox(0);
    tracker->info[i].bbox.y1 = t_bbox(1);
    tracker->info[i].bbox.x2 = t_bbox(0) + t_bbox(2);
    tracker->info[i].bbox.y2 = t_bbox(1) + t_bbox(3);
    face->info[i].unique_id = t_id;
    tracker->info[i].id = t_id;
  }
#ifdef DEBUG_TRACK
  std::cout << "finish track,face num:" << face->size << std::endl;
  show_INFO_KalmanTrackers();
#endif
  return CVI_TDL_SUCCESS;
}