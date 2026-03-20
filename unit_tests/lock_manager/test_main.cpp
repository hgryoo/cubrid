/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#include "test_lock_manager_scenarios.hpp"
#include "boot_sr.h"
#include "critical_section.h"
#include "error_manager.h"
#include "event_log.h"
#include "language_support.h"
#include "log_impl.h"
#include "object_domain.h"
#include "page_buffer.h"
#include "thread_manager.hpp"

#include <cstdlib>
#include <exception>
#include <new>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
  class scoped_thread_environment
  {
    public:
	scoped_thread_environment ()
	  : m_main_entry (NULL)
	, m_tdes_area (NULL)
	, m_tdes_count (4096)
	, m_error_initialized (false)
	, m_thread_initialized (false)
	, m_csect_initialized (false)
	, m_event_log_initialized (false)
	, m_pgbuf_initialized (false)
      {
	if (er_init (NULL, ER_NEVER_EXIT) != NO_ERROR)
	  {
	    throw std::runtime_error ("failed to initialize error manager");
	  }
	m_error_initialized = true;

	if (lang_init () != NO_ERROR)
	  {
	    throw std::runtime_error ("failed to initialize language support");
	  }
	if (lang_set_charset_lang ("en_US.iso88591") != NO_ERROR)
	  {
	    throw std::runtime_error ("failed to set language charset");
	  }
	if (tp_init () != NO_ERROR)
	  {
	    throw std::runtime_error ("failed to initialize type domains");
	  }

	cubthread::initialize (m_main_entry);
	if (cubthread::initialize_thread_entries () != NO_ERROR)
	  {
	    throw std::runtime_error ("failed to initialize thread entries");
	  }
	m_thread_initialized = true;
	boot_server_status (BOOT_SERVER_UP);
	log_Gl.rcv_phase = LOG_RESTARTED;

	if (csect_initialize_static_critical_sections () != NO_ERROR)
	  {
	    throw std::runtime_error ("failed to initialize critical sections");
	  }
	m_csect_initialized = true;

	event_log_init ("test_lock_manager");
	m_event_log_initialized = true;

	initialize_fake_tdes ();

	if (pgbuf_initialize () != NO_ERROR)
	  {
	    throw std::runtime_error ("failed to initialize page buffer");
	  }
	m_pgbuf_initialized = true;
      }

      ~scoped_thread_environment ()
      {
	if (m_pgbuf_initialized)
	  {
	    pgbuf_finalize ();
	  }
	if (m_event_log_initialized)
	  {
	    event_log_final ();
	  }
	if (m_csect_initialized)
	  {
	    csect_finalize_static_critical_sections ();
	  }
	finalize_fake_tdes ();
	if (m_thread_initialized)
	  {
	    boot_server_status (BOOT_SERVER_DOWN);
	    cubthread::return_lock_free_transaction_entries ();
	    cubthread::finalize ();
	  }
	if (m_error_initialized)
	  {
	    er_final (ER_ALL_FINAL);
	  }
      }

    private:
      void
      initialize_fake_tdes ()
      {
	size_t ptr_array_size = m_tdes_count * sizeof (*log_Gl.trantable.all_tdes);
	size_t area_size = sizeof (LOG_ADDR_TDESAREA) + m_tdes_count * sizeof (LOG_TDES);

	log_Gl.trantable = TRANTABLE_INITIALIZER;
	log_Gl.trantable.all_tdes = (LOG_TDES **) malloc (ptr_array_size);
	if (log_Gl.trantable.all_tdes == NULL)
	  {
	    throw std::bad_alloc ();
	  }

	m_tdes_area = (LOG_ADDR_TDESAREA *) malloc (area_size);
	if (m_tdes_area == NULL)
	  {
	    throw std::bad_alloc ();
	  }

	m_tdes_area->tdesarea = (LOG_TDES *) ((char *) m_tdes_area + sizeof (LOG_ADDR_TDESAREA));
	m_tdes_area->next = NULL;

	for (int index = 0; index < m_tdes_count; index++)
	  {
	    LOG_TDES *tdes = &m_tdes_area->tdesarea[index];
	    log_Gl.trantable.all_tdes[index] = tdes;
	    logtb_initialize_tdes (tdes, index);
	  }

	log_Gl.trantable.area = m_tdes_area;
	log_Gl.trantable.num_total_indices = m_tdes_count;
	log_Gl.trantable.num_assigned_indices = m_tdes_count;
      }

      void
      finalize_fake_tdes ()
      {
	if (log_Gl.trantable.all_tdes != NULL)
	  {
	    free (log_Gl.trantable.all_tdes);
	    log_Gl.trantable.all_tdes = NULL;
	  }
	if (m_tdes_area != NULL)
	  {
	    free (m_tdes_area);
	    m_tdes_area = NULL;
	  }
	log_Gl.trantable = TRANTABLE_INITIALIZER;
      }

      cubthread::entry *m_main_entry;
      LOG_ADDR_TDESAREA *m_tdes_area;
      int m_tdes_count;
      bool m_error_initialized;
      bool m_thread_initialized;
      bool m_csect_initialized;
      bool m_event_log_initialized;
      bool m_pgbuf_initialized;
  };

  void
  print_usage (const char *progname)
  {
    std::cout << "Usage: " << progname
              << " [--list] [--functional] [--benchmark] [--scenario <name>]"
              << " [--transaction N] [--iterations N] [--hotset N] [--class-count N]"
              << " [--objects-per-class N] [--hot-ratio N] [--collision-ratio N]"
              << " [--sample N] [--loops N] [--benchmark-format csv|pretty|both]" << std::endl;
  }

  int
  parse_int (const char *arg_name, const char *value)
  {
    char *end_ptr = NULL;
    long parsed = std::strtol (value, &end_ptr, 10);
    if (end_ptr == value || *end_ptr != '\0' || parsed <= 0)
      {
        throw std::invalid_argument (std::string ("Invalid value for ") + arg_name + ": " + value);
      }
    return static_cast<int> (parsed);
  }

  int
  parse_ratio (const char *arg_name, const char *value)
  {
    const int ratio = parse_int (arg_name, value);
    if (ratio > 100)
      {
        throw std::invalid_argument (std::string ("Invalid ratio for ") + arg_name + ": " + value);
      }
    return ratio;
  }
}

int
main (int argc, char **argv)
{
  try
    {
      test_lock_manager::scenario_config config {
        test_lock_manager::scenario_kind::hot_row,
        4,
        10,
        4,
        4,
        16,
        70,
        25,
        1
      };
      int sample_count = 8;
      int benchmark_loops = 10;
      test_lock_manager::benchmark_output_format benchmark_format = test_lock_manager::benchmark_output_format::both;
      bool hotset_explicit = false;
      bool list_only = false;
      bool run_functional = false;
      bool run_benchmark = false;

      for (int index = 1; index < argc; index++)
        {
          std::string arg = argv[index];
          if (arg == "--list")
            {
              list_only = true;
            }
          else if (arg == "--functional")
            {
              run_functional = true;
            }
          else if (arg == "--benchmark")
            {
              run_benchmark = true;
            }
          else if (arg == "--scenario" && index + 1 < argc)
            {
              config.kind = test_lock_manager::parse_scenario_kind (argv[++index]);
            }
          else if (arg == "--transaction" && index + 1 < argc)
            {
              config.transaction_count = parse_int ("--transaction", argv[++index]);
            }
          else if (arg == "--iterations" && index + 1 < argc)
            {
              config.iterations = parse_int ("--iterations", argv[++index]);
            }
          else if (arg == "--hotset" && index + 1 < argc)
            {
              config.hotset_size = parse_int ("--hotset", argv[++index]);
              hotset_explicit = true;
            }
          else if (arg == "--class-count" && index + 1 < argc)
            {
              config.class_count = parse_int ("--class-count", argv[++index]);
            }
          else if (arg == "--objects-per-class" && index + 1 < argc)
            {
              config.objects_per_class = parse_int ("--objects-per-class", argv[++index]);
            }
          else if (arg == "--hot-ratio" && index + 1 < argc)
            {
              config.hot_ratio = parse_ratio ("--hot-ratio", argv[++index]);
            }
          else if (arg == "--collision-ratio" && index + 1 < argc)
            {
              config.collision_ratio = parse_ratio ("--collision-ratio", argv[++index]);
            }
          else if (arg == "--sample" && index + 1 < argc)
            {
              sample_count = parse_int ("--sample", argv[++index]);
            }
          else if (arg == "--loops" && index + 1 < argc)
            {
              benchmark_loops = parse_int ("--loops", argv[++index]);
            }
          else if (arg == "--benchmark-format" && index + 1 < argc)
            {
              benchmark_format = test_lock_manager::parse_benchmark_output_format (argv[++index]);
            }
          else
            {
              print_usage (argv[0]);
              throw std::invalid_argument ("Unknown argument: " + arg);
            }
        }

      if (!hotset_explicit)
        {
          config.hotset_size = std::max (config.objects_per_class / 4, 1);
        }

      if (list_only)
        {
          std::cout << "Available scenarios:" << std::endl;
          for (const std::string &name : test_lock_manager::get_scenario_names ())
            {
              test_lock_manager::scenario_kind kind = test_lock_manager::parse_scenario_kind (name);
              std::cout << "  - " << name << ": " << test_lock_manager::get_scenario_description (kind) << std::endl;
            }
          return 0;
        }

      if (run_functional)
        {
          scoped_thread_environment thread_env;
          return test_lock_manager::run_functional_suite ();
        }

      if (run_benchmark)
        {
          scoped_thread_environment thread_env;
          return test_lock_manager::run_benchmark_suite (benchmark_loops, config, benchmark_format);
        }

      const std::vector<test_lock_manager::operation> operations = test_lock_manager::build_operations (config);
      const test_lock_manager::scenario_summary summary = test_lock_manager::summarize (config.kind, operations);

      std::cout << "Scenario: " << summary.name << std::endl;
      std::cout << "Description: " << summary.description << std::endl;
      std::cout << "Transactions: " << config.transaction_count << std::endl;
      std::cout << "Iterations: " << config.iterations << std::endl;
      std::cout << "Hotset size per class: " << config.hotset_size << std::endl;
      std::cout << "Class count: " << config.class_count << std::endl;
      std::cout << "Objects per class: " << config.objects_per_class << std::endl;
      std::cout << "Hot access ratio: " << config.hot_ratio << std::endl;
      std::cout << "Collision access ratio: " << config.collision_ratio << std::endl;
      std::cout << "Operations: " << summary.operation_count << std::endl;
      std::cout << "Distinct classes: " << summary.distinct_class_count << std::endl;
      std::cout << "Distinct OIDs: " << summary.distinct_oid_count << std::endl;

      std::cout << "Operation kind counts:" << std::endl;
      for (const auto &entry : summary.operation_kind_counts)
        {
          std::cout << "  - " << entry.first << ": " << entry.second << std::endl;
        }

      std::cout << "Lock mode counts:" << std::endl;
      for (const auto &entry : summary.lock_mode_counts)
        {
          std::cout << "  - " << entry.first << ": " << entry.second << std::endl;
        }

      std::cout << "Sample operations:" << std::endl;
      for (int idx = 0; idx < sample_count && idx < static_cast<int> (operations.size ()); idx++)
        {
          std::cout << "  - " << test_lock_manager::format_operation (operations[idx]) << std::endl;
        }

      return 0;
    }
  catch (const std::exception &e)
    {
      std::cerr << "test_lock_manager failed: " << e.what () << std::endl;
      return 1;
    }
}
