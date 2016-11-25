--TEST--
APM: commandStarted callback
--SKIPIF--
skip APM temporarily removed
--FILE--
<?php
include dirname(__FILE__) . '/../utils.inc';

$m = new MongoDB\Driver\Manager("mongodb://localhost:27017");
cleanup( $m );

class MySubscriber implements MongoDB\Driver\Monitoring\CommandSubscriber
{
	public function commandStarted( \MongoDB\Driver\Monitoring\CommandStartedEvent $event )
	{
		echo "started: ", $event->getCommandName(), "\n";
		echo "- getCommand() returns an object: ", is_object( $event->getCommand() ) ? 'yes' : 'no', "\n";
		echo "- getCommand() returns a stdClass object: ", $event->getCommand() instanceof stdClass ? 'yes' : 'no', "\n";
		echo "- getDatabaseName() returns a string: ", is_string( $event->getDatabaseName() ) ? 'yes' : 'no', "\n";
		echo "- getDatabaseName() returns '", $event->getDatabaseName(), "'\n";
		echo "- getCommandName() returns a string: ", is_string( $event->getCommandName() ) ? 'yes' : 'no', "\n";
		echo "- getCommandName() returns '", $event->getCommandName(), "'\n";
		echo "- getServer() returns an object: ", is_object( $event->getServer() ) ? 'yes' : 'no', "\n";
		echo "- getServer() returns a Server object: ", $event->getServer() instanceof MongoDB\Driver\Server ? 'yes' : 'no', "\n";
		echo "- getOperationId() returns a string: ", is_string( $event->getOperationId() ) ? 'yes' : 'no', "\n";
		echo "- getRequestId() returns a string: ", is_string( $event->getRequestId() ) ? 'yes' : 'no', "\n";
	}

	public function commandSucceeded( \MongoDB\Driver\Monitoring\CommandSucceededEvent $event )
	{
	}

	public function commandFailed( \MongoDB\Driver\Monitoring\CommandFailedEvent $event )
	{
	}
}

$query = new MongoDB\Driver\Query( [] );
$subscriber = new MySubscriber;

$m->addSubscriber( $subscriber );

$cursor = $m->executeQuery( "demo.test", $query );
?>
--EXPECT--
started: find
- getCommand() returns an object: yes
- getCommand() returns a stdClass object: yes
- getDatabaseName() returns a string: yes
- getDatabaseName() returns 'demo'
- getCommandName() returns a string: yes
- getCommandName() returns 'find'
- getServer() returns an object: yes
- getServer() returns a Server object: yes
- getOperationId() returns a string: yes
- getRequestId() returns a string: yes
