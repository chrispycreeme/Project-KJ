import 'package:cloud_firestore/cloud_firestore.dart';

import '../models/trap_data.dart';
import '../models/trap_log_entry.dart';

class TrapRepository {
  TrapRepository({FirebaseFirestore? firestore})
      : _firestore = firestore ?? FirebaseFirestore.instance;

  final FirebaseFirestore _firestore;

  static const String _collection = 'trap_controller';
  static const String _documentId = 'primary';

  DocumentReference<Map<String, dynamic>> get _document => _firestore
      .collection(_collection)
      .doc(_documentId);

  CollectionReference<Map<String, dynamic>> get _logsCollection => _document
      .collection('logs');

  Future<void> bootstrap() async {
    final snapshot = await _document.get();
    if (!snapshot.exists) {
      await _document.set(TrapData.defaults().toMap());
    }
  }

  Stream<TrapData> watchTrapData() {
    return _document.snapshots().map((snapshot) {
      if (!snapshot.exists) {
        return TrapData.defaults();
      }
      return TrapData.fromSnapshot(snapshot);
    });
  }

  Stream<List<TrapLogEntry>> watchRecentLogs({int limit = 20}) {
    return _logsCollection
        .orderBy('timestamp', descending: true)
        .limit(limit)
        .snapshots()
        .map((query) =>
            query.docs.map(TrapLogEntry.fromSnapshot).toList(growable: false));
  }

  Future<void> updateControls({
    bool? activateTrap,
    bool? openFeedDispenser,
    bool? co2Release,
    int? cyclesNeededForCo2Release,
  }) async {
    final update = <String, dynamic>{};

    if (activateTrap != null) {
      update['activate_trap'] = activateTrap;
    }
    if (openFeedDispenser != null) {
      update['open_feed_dispenser'] = openFeedDispenser;
    }
    if (co2Release != null) {
      update['co2_release'] = co2Release;
    }
    if (cyclesNeededForCo2Release != null) {
      update['cycles_needed_for_co2_release'] = cyclesNeededForCo2Release;
    }

    if (update.isEmpty) {
      return;
    }

    update['updated_at'] = FieldValue.serverTimestamp();

    await _document.set(update, SetOptions(merge: true));
  }

  Future<void> appendLog({
    required String label,
    Map<String, dynamic>? payload,
  }) async {
    await _logsCollection.add({
      'label': label,
      'payload': payload ?? <String, dynamic>{},
      'timestamp': FieldValue.serverTimestamp(),
    });
  }
}
