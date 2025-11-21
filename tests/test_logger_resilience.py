import sys
import os
import unittest
import json
from pathlib import Path
from unittest.mock import MagicMock, patch, call
import time

# Add repo root and .ai path to import builder agent
repo_root = Path(__file__).parent.parent
sys.path.append(str(repo_root / ".ai"))

# Mock environment variables
os.environ["OPENROUTER_API_KEY"] = "mock_key"
os.environ["GOOGLE_API_KEY"] = "mock_key"
os.environ["AGENT_LOG_LEVEL"] = "DEBUG"

# Mock langchain dependencies
sys.modules["langchain_openai"] = MagicMock()
sys.modules["langchain_google_genai"] = MagicMock()
sys.modules["langchain_core.messages"] = MagicMock()
sys.modules["langgraph.graph"] = MagicMock()

from builder_agent.logger import AgentLogger, LogLevel
from builder_agent.resilience import (
    CircuitBreaker,
    CircuitBreakerOpenError,
    retry_with_backoff,
    fallback_parse_json
)


class TestLogger(unittest.TestCase):
    """Test the structured logging module."""
    
    def setUp(self):
        self.logger = AgentLogger(level="DEBUG")
    
    def test_log_levels(self):
        """Test that log levels filter correctly."""
        info_logger = AgentLogger(level="INFO")
        
        # Should not trigger DEBUG logs
        with patch('sys.stderr.write') as mock:
            info_logger.debug("test message")
            # Debug shouldn't output at INFO level
            self.assertEqual(mock.call_count, 0)
    
    def test_node_logging(self):
        """Test node entry/exit logging."""
        self.logger.log_node_entry("test_node", task_id="123")
        self.logger.log_node_exit("test_node", task_id="123", duration=1.5, result={"success": True})
        
        # Check metrics
        metrics = self.logger.get_metrics()
        self.assertEqual(metrics['node_calls']['test_node'], 1)
        self.assertIn('test_node', metrics['average_node_durations'])
    
    def test_llm_call_logging(self):
        """Test LLM call logging."""
        self.logger.log_llm_call(
            model="test-model",
            prompt_tokens=100,
            response_tokens=50,
            duration=2.0,
            success=True
        )
        
        metrics = self.logger.get_metrics()
        self.assertEqual(metrics['llm_calls'], 1)
        self.assertEqual(metrics['llm_tokens'], 150)
    
    def test_tool_execution_logging(self):
        """Test tool execution logging."""
        self.logger.log_tool_execution(
            tool_name="test_tool",
            params={"arg": "value"},
            result="success",
            duration=0.5,
            success=True
        )
        
        metrics = self.logger.get_metrics()
        self.assertEqual(metrics['tool_executions']['test_tool'], 1)
    
    def test_retry_logging(self):
        """Test retry attempt logging."""
        self.logger.log_retry("test_operation", attempt=1, max_attempts=3, error="Test error")
        
        metrics = self.logger.get_metrics()
        self.assertEqual(metrics['retries'], 1)
    
    def test_circuit_breaker_logging(self):
        """Test circuit breaker logging."""
        self.logger.log_circuit_breaker_trip("test_service", failure_count=5)
        
        metrics = self.logger.get_metrics()
        self.assertEqual(metrics['circuit_breaker_trips'], 1)
    
    def test_error_logging(self):
        """Test error logging."""
        self.logger.error("Test error", error_type="test_type")
        
        metrics = self.logger.get_metrics()
        self.assertEqual(metrics['errors']['test_type'], 1)


class TestCircuitBreaker(unittest.TestCase):
    """Test circuit breaker functionality."""
    
    def test_normal_operation(self):
        """Test circuit breaker in closed state."""
        cb = CircuitBreaker(failure_threshold=3, timeout_seconds=1, name="test")
        
        def success_func():
            return "success"
        
        result = cb.call(success_func)
        self.assertEqual(result, "success")
        self.assertFalse(cb.is_open())
    
    def test_opens_after_threshold(self):
        """Test circuit breaker opens after threshold failures."""
        cb = CircuitBreaker(failure_threshold=3, timeout_seconds=1, name="test")
        
        def failing_func():
            raise ValueError("Test error")
        
        # Fail 3 times to hit threshold
        for i in range(3):
            with self.assertRaises(ValueError):
                cb.call(failing_func)
        
        # Circuit should now be open
        self.assertTrue(cb.is_open())
        
        # Next call should raise CircuitBreakerOpenError
        with self.assertRaises(CircuitBreakerOpenError):
            cb.call(failing_func)
    
    def test_half_open_recovery(self):
        """Test circuit breaker recovery through half-open state."""
        cb = CircuitBreaker(failure_threshold=2, timeout_seconds=0.5, name="test")
        
        def failing_func():
            raise ValueError("Test error")
        
        # Open the circuit
        for i in range(2):
            with self.assertRaises(ValueError):
                cb.call(failing_func)
        
        self.assertTrue(cb.is_open())
        
        # Wait for timeout
        time.sleep(0.6)
        
        # Should enter half-open and allow a test call
        def success_func():
            return "success"
        
        result = cb.call(success_func)
        self.assertEqual(result, "success")
        self.assertFalse(cb.is_open())  # Should be closed now
    
    def test_manual_reset(self):
        """Test manual circuit breaker reset."""
        cb = CircuitBreaker(failure_threshold=2, timeout_seconds=1, name="test")
        
        def failing_func():
            raise ValueError("Test error")
        
        # Open the circuit
        for i in range(2):
            with self.assertRaises(ValueError):
                cb.call(failing_func)
        
        self.assertTrue(cb.is_open())
        
        # Manual reset
        cb.reset()
        self.assertFalse(cb.is_open())


class TestRetryMechanism(unittest.TestCase):
    """Test retry decorator functionality."""
    
    def test_successful_first_try(self):
        """Test that successful calls don't retry."""
        call_count = []
        
        @retry_with_backoff(max_retries=3, backoff_base=0.1)
        def success_func():
            call_count.append(1)
            return "success"
        
        result = success_func()
        self.assertEqual(result, "success")
        self.assertEqual(len(call_count), 1)
    
    def test_retry_on_failure(self):
        """Test that failures trigger retries."""
        call_count = []
        
        @retry_with_backoff(max_retries=2, backoff_base=0.1)
        def failing_func():
            call_count.append(1)
            if len(call_count) < 3:
                raise ValueError("Test error")
            return "success"
        
        result = failing_func()
        self.assertEqual(result, "success")
        self.assertEqual(len(call_count), 3)  # Initial + 2 retries
    
    def test_max_retries_exceeded(self):
        """Test that max retries is enforced."""
        call_count = []
        
        @retry_with_backoff(max_retries=2, backoff_base=0.1)
        def always_fails():
            call_count.append(1)
            raise ValueError("Test error")
        
        with self.assertRaises(ValueError):
            always_fails()
        
        self.assertEqual(len(call_count), 3)  # Initial + 2 retries
    
    def test_exponential_backoff(self):
        """Test that backoff delays increase exponentially."""
        timestamps = []
        
        @retry_with_backoff(max_retries=3, backoff_base=0.1)
        def failing_func():
            timestamps.append(time.time())
            if len(timestamps) < 4:
                raise ValueError("Test error")
            return "success"
        
        failing_func()
        
        # Check that delays increase
        delays = [timestamps[i+1] - timestamps[i] for i in range(len(timestamps)-1)]
        # Each delay should be roughly 0.1^attempt
        self.assertTrue(delays[0] < delays[1])  # Second delay > first delay


class TestFallbackParsing(unittest.TestCase):
    """Test fallback JSON parsing strategies."""
    
    def test_direct_json(self):
        """Test parsing direct JSON."""
        content = '{"key": "value"}'
        result = fallback_parse_json(content)
        self.assertEqual(result, {"key": "value"})
    
    def test_json_code_block(self):
        """Test parsing JSON from markdown code block."""
        content = '```json\n{"key": "value"}\n```'
        result = fallback_parse_json(content)
        self.assertEqual(result, {"key": "value"})
    
    def test_code_block(self):
        """Test parsing JSON from generic code block."""
        content = '```\n{"key": "value"}\n```'
        result = fallback_parse_json(content)
        self.assertEqual(result, {"key": "value"})
    
    def test_embedded_json(self):
        """Test extracting JSON from text."""
        content = 'Some text before {"key": "value"} some text after'
        result = fallback_parse_json(content)
        self.assertEqual(result, {"key": "value"})
    
    def test_invalid_json(self):
        """Test that invalid JSON returns None."""
        content = 'not json at all'
        result = fallback_parse_json(content)
        self.assertIsNone(result)
    
    def test_nested_objects(self):
        """Test parsing nested JSON objects."""
        content = '{"outer": {"inner": "value"}}'
        result = fallback_parse_json(content)
        self.assertEqual(result, {"outer": {"inner": "value"}})


if __name__ == "__main__":
    unittest.main()
