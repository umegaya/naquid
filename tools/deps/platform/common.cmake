set(common_src
	./ext/chromium/base/at_exit.cc
	./ext/chromium/base/base64.cc
	./ext/chromium/base/callback_internal.cc
	./ext/chromium/base/debug/alias.cc
	./ext/chromium/base/debug/debugger.cc
	./ext/chromium/base/debug/stack_trace.cc
	./ext/chromium/base/lazy_instance.cc
	./ext/chromium/base/location.cc
	./ext/chromium/base/logging.cc
	./ext/chromium/base/memory/singleton.cc
	./ext/chromium/base/pickle.cc
	./ext/chromium/base/rand_util.cc
	./ext/chromium/base/strings/string_number_conversions.cc
	./ext/chromium/base/strings/string_piece.cc
	./ext/chromium/base/strings/string_split.cc
	./ext/chromium/base/strings/string_util.cc
	./ext/chromium/base/strings/string_util_constants.cc
	./ext/chromium/base/strings/string16.cc
	./ext/chromium/base/strings/stringprintf.cc
	./ext/chromium/base/strings/utf_offset_string_conversions.cc
	./ext/chromium/base/strings/utf_string_conversions.cc
	./ext/chromium/base/strings/utf_string_conversion_utils.cc
	./ext/chromium/base/synchronization/lock.cc
	./ext/chromium/base/time/time.cc
	./ext/chromium/base/third_party/nspr/prtime.cc
	./ext/chromium/base/third_party/dmg_fp/dtoa_wrapper.cc
	./ext/chromium/base/third_party/dmg_fp/g_fmt.cc
	./ext/chromium/base/third_party/icu/icu_utf.cc
	./ext/chromium/base/threading/thread_id_name_manager.cc
	./ext/chromium/base/threading/thread_local_storage.cc
	./ext/chromium/base/threading/thread_restrictions.cc
	./ext/chromium/base/trace_event/memory_usage_estimator.cc

	./ext/chromium/crypto/hkdf.cc
	./ext/chromium/crypto/hmac.cc
	./ext/chromium/crypto/openssl_util.cc
	./ext/chromium/crypto/random.cc
	./ext/chromium/crypto/secure_util.cc
	./ext/chromium/crypto/symmetric_key.cc

	./ext/chromium/third_party/modp_b64/modp_b64.cc

	./ext/chromium/url/url_constants.cc
	./ext/chromium/url/url_canon_etc.cc
	./ext/chromium/url/url_canon_filesystemurl.cc
	./ext/chromium/url/url_canon_fileurl.cc
	./ext/chromium/url/url_canon_host.cc
	./ext/chromium/url/url_canon_internal.cc
	./ext/chromium/url/url_canon_ip.cc
	./ext/chromium/url/url_canon_mailtourl.cc
	./ext/chromium/url/url_canon_path.cc
	./ext/chromium/url/url_canon_pathurl.cc
	./ext/chromium/url/url_canon_query.cc
	./ext/chromium/url/url_canon_relative.cc
	./ext/chromium/url/url_canon_stdstring.cc
	./ext/chromium/url/url_canon_stdurl.cc
	./ext/chromium/url/url_util.cc
	./ext/chromium/url/url_parse_file.cc
	./ext/chromium/url/gurl.cc
	./ext/chromium/url/third_party/mozilla/url_parse.cc

	# ./ext/chromium/url/url_canon_icu.cc | TODO(iyatomi): temporary removed. if its necessary for secure handshake, recover back.
)

set(common_src_bk
	./ext/chromium/base/allocator/allocator_extension.cc
	./ext/chromium/base/at_exit.cc
	./ext/chromium/base/base64.cc
	./ext/chromium/base/base_paths.cc
	./ext/chromium/base/base_switches.cc
	./ext/chromium/base/bind_helpers.cc
	./ext/chromium/base/build_time.cc
	./ext/chromium/base/callback_helpers.cc
	./ext/chromium/base/callback_internal.cc
	./ext/chromium/base/command_line.cc
	./ext/chromium/base/cpu.cc
	./ext/chromium/base/debug/alias.cc
	./ext/chromium/base/debug/debugger.cc
	./ext/chromium/base/debug/dump_without_crashing.cc
	./ext/chromium/base/debug/profiler.cc
	./ext/chromium/base/debug/stack_trace.cc
	./ext/chromium/base/debug/task_annotator.cc
	./ext/chromium/base/debug/thread_heap_usage_tracker.cc
	./ext/chromium/base/environment.cc
	./ext/chromium/base/feature_list.cc
	./ext/chromium/base/files/file.cc
	./ext/chromium/base/files/file_enumerator.cc
	./ext/chromium/base/files/file_path.cc
	./ext/chromium/base/files/file_tracing.cc
	./ext/chromium/base/files/file_util.cc
	./ext/chromium/base/files/memory_mapped_file.cc
	./ext/chromium/base/files/scoped_file.cc
	./ext/chromium/base/guid.cc
	./ext/chromium/base/hash.cc
	./ext/chromium/base/json/json_parser.cc
	./ext/chromium/base/json/json_reader.trace_config_category_filter
	./ext/chromium/base/json/json_string_value_serializer.cc
	./ext/chromium/base/json/json_writer.cc
	./ext/chromium/base/json/string_escape.cc
	./ext/chromium/base/lazy_instance.cc
	./ext/chromium/base/location.cc
	./ext/chromium/base/logging.cc
	./ext/chromium/base/mac/dispatch_source_mach.cc
	./ext/chromium/base/mac/mach_logging.cc
	./ext/chromium/base/mac/scoped_mach_port.cc
	./ext/chromium/base/mac/scoped_mach_vm.cc
	./ext/chromium/base/memory/aligned_memory.cc
	./ext/chromium/base/memory/ref_counted.cc
	./ext/chromium/base/memory/ref_counted_memory.cc
	./ext/chromium/base/memory/shared_memory_handle.cc
	./ext/chromium/base/memory/shared_memory_helper.cc
	./ext/chromium/base/memory/shared_memory_tracker.cc
	./ext/chromium/base/memory/singleton.cc
	./ext/chromium/base/memory/weak_ptr.cc
	./ext/chromium/base/metrics/bucket_ranges.cc
	./ext/chromium/base/metrics/field_trial.cc
	./ext/chromium/base/metrics/field_trial_param_associator.cc
	./ext/chromium/base/metrics/field_trial_params.cc
	./ext/chromium/base/metrics/histogram_base.cc
	./ext/chromium/base/metrics/histogram_samples.cc
	./ext/chromium/base/metrics/persistent_memory_allocator.cc
	./ext/chromium/base/nix/xdg_util.cc
	./ext/chromium/base/path_service.cc
	./ext/chromium/base/pending_task.cc
	./ext/chromium/base/pickle.cc
	./ext/chromium/base/posix/file_descriptor_shuffle.cc
	./ext/chromium/base/posix/global_descriptors.cc
	./ext/chromium/base/posix/safe_strerror.cc
	./ext/chromium/base/process/kill.cc
	./ext/chromium/base/process/launch.cc
	./ext/chromium/base/process/memory.cc
	./ext/chromium/base/process/process_handle.cc
	./ext/chromium/base/process/process_iterator.cc
	./ext/chromium/base/process/process_metrics.cc
	./ext/chromium/base/run_loop.cc
	./ext/chromium/base/sequence_token.cc
	./ext/chromium/base/sequenced_task_runner.cc
	./ext/chromium/base/sha1.cc
	./ext/chromium/base/strings/pattern.cc
	./ext/chromium/base/strings/string16.cc
	./ext/chromium/base/strings/string_number_conversions.cc
	./ext/chromium/base/strings/string_piece.cc
	./ext/chromium/base/strings/string_split.cc
	./ext/chromium/base/strings/string_util.cc
	./ext/chromium/base/strings/stringprintf.cc
	./ext/chromium/base/strings/utf_string_conversion_utils.cc
	./ext/chromium/base/strings/utf_string_conversions.cc
	./ext/chromium/base/synchronization/atomic_flag.cc
	./ext/chromium/base/sys_info.cc
	./ext/chromium/base/task_runner.cc
	./ext/chromium/base/task_scheduler/delayed_task_manager.cc
	./ext/chromium/base/task_scheduler/environment_config.cc
	./ext/chromium/base/task_scheduler/post_task.cc
	./ext/chromium/base/task_scheduler/priority_queue.cc
	./ext/chromium/base/task_scheduler/scheduler_lock_impl.cc
	./ext/chromium/base/task_scheduler/scheduler_single_thread_task_runner_manager.cc
	./ext/chromium/base/task_scheduler/scheduler_worker.cc
	./ext/chromium/base/task_scheduler/scheduler_worker_pool.cc
	./ext/chromium/base/task_scheduler/scheduler_worker_pool_impl.cc
	./ext/chromium/base/task_scheduler/scheduler_worker_pool_params.cc
	./ext/chromium/base/task_scheduler/scheduler_worker_stack.cc
	./ext/chromium/base/task_scheduler/scoped_set_task_priority_for_current_thread.cc
	./ext/chromium/base/task_scheduler/sequence.cc
	./ext/chromium/base/task_scheduler/sequence_sort_key.cc
	./ext/chromium/base/task_scheduler/task.cc
	./ext/chromium/base/task_scheduler/task_scheduler.cc
	./ext/chromium/base/task_scheduler/task_scheduler_impl.cc
	./ext/chromium/base/task_scheduler/task_tracker.cc
	./ext/chromium/base/task_scheduler/task_traits.cc
	./ext/chromium/base/third_party/icu/icu_utf.cc
	./ext/chromium/base/third_party/nspr/prtime.cc
	./ext/chromium/base/third_party/xdg_user_dirs/xdg_user_dir_lookup.cc
	./ext/chromium/base/threading/post_task_and_reply_impl.cc
	./ext/chromium/base/threading/scoped_blocking_call.cc
	./ext/chromium/base/threading/sequence_local_storage_map.cc
	./ext/chromium/base/threading/sequenced_task_runner_handle.cc
	./ext/chromium/base/threading/sequenced_worker_pool.cc
	./ext/chromium/base/threading/simple_thread.cc
	./ext/chromium/base/threading/thread.cc
	./ext/chromium/base/threading/thread_checker_impl.cc
	./ext/chromium/base/threading/thread_collision_warner.cc
	./ext/chromium/base/threading/thread_id_name_manager.cc
	./ext/chromium/base/threading/thread_local_storage.cc
	./ext/chromium/base/threading/thread_restrictions.cc
	./ext/chromium/base/threading/thread_task_runner_handle.cc
	./ext/chromium/base/time/default_tick_clock.cc
	./ext/chromium/base/time/tick_clock.cc
	./ext/chromium/base/time/time.cc
	./ext/chromium/base/timer/elapsed_timer.cc
	./ext/chromium/base/timer/timer.cc
	./ext/chromium/base/trace_event/category_registry.cc
	./ext/chromium/base/trace_event/event_name_filter.cc
	./ext/chromium/base/trace_event/heap_profiler_allocation_context.cc
	./ext/chromium/base/trace_event/heap_profiler_allocation_context_tracker.cc
	./ext/chromium/base/trace_event/heap_profiler_allocation_register.cc
	./ext/chromium/base/trace_event/heap_profiler_event_filter.cc
	./ext/chromium/base/trace_event/heap_profiler_heap_dump_writer.cc
	./ext/chromium/base/trace_event/heap_profiler_serialization_state.cc
	./ext/chromium/base/trace_event/heap_profiler_stack_frame_deduplicator.cc
	./ext/chromium/base/trace_event/heap_profiler_type_name_deduplicator.cc
	./ext/chromium/base/trace_event/malloc_dump_provider.cc
	./ext/chromium/base/trace_event/memory_allocator_dump.cc
	./ext/chromium/base/trace_event/memory_allocator_dump_guid.cc
	./ext/chromium/base/trace_event/memory_dump_manager.cc
	./ext/chromium/base/trace_event/memory_dump_provider_info.cc
	./ext/chromium/base/trace_event/memory_dump_request_args.cc
	./ext/chromium/base/trace_event/memory_dump_scheduler.cc
	./ext/chromium/base/trace_event/memory_infra_background_whitelist.cc
	./ext/chromium/base/trace_event/memory_peak_detector.cc
	./ext/chromium/base/trace_event/memory_usage_estimator.cc
	./ext/chromium/base/trace_event/process_memory_dump.cc
	./ext/chromium/base/trace_event/sharded_allocation_register.cc
	./ext/chromium/base/trace_event/trace_buffer.cc
	./ext/chromium/base/trace_event/trace_config.cc
	./ext/chromium/base/trace_event/trace_config_category_filter.cc
	./ext/chromium/base/trace_event/trace_event_argument.cc
	./ext/chromium/base/trace_event/trace_event_filter.cc
	./ext/chromium/base/trace_event/trace_event_impl.cc
	./ext/chromium/base/trace_event/trace_event_memory_overhead.cc
	./ext/chromium/base/trace_event/trace_event_system_stats_monitor.cc
	./ext/chromium/base/trace_event/trace_log.cc
	./ext/chromium/base/unguessable_token.cc
	./ext/chromium/base/value_iterators.cc
	./ext/chromium/base/values.cc
	./ext/chromium/base/vlog.cc
	./ext/chromium/crypto/hkdf.cc
	./ext/chromium/crypto/hmac.cc
	./ext/chromium/crypto/openssl_util.cc
	./ext/chromium/crypto/random.cc
	./ext/chromium/crypto/secure_util.cc
	./ext/chromium/crypto/symmetric_key.cc
	./ext/chromium/third_party/modp_b64/modp_b64.cc
	./ext/chromium/url/url_canon_internal.cc
	./ext/chromium/url/url_canon_ip.cc
	./ext/chromium/url/url_canon_stdstring.cc
	./ext/chromium/url/url_constants.cc
	./ext/chromium/naquid.cpp
)